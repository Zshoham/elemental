//! Sketch: Elemental's allocator contract next to Rust's. Compiled, linked
//! against the C static libraries, and run by `ctest -R interop.rust_alloc`.
//!
//! This is not a binding. It checks that `el_allocator` and Rust's
//! `GlobalAlloc` describe the same contract, in both directions:
//!
//!   1. A C allocator used from Rust: `ElGlobal` implements `GlobalAlloc`
//!      over an `el_stack_arena`.
//!   2. A Rust allocator used from C: `RustAllocator` hands Rust's global
//!      allocator to C as an `el_allocator`, and C's `el_grow_writer` grows
//!      in it.
//!
//! What lines up: null on failure; a power-of-two alignment on every
//! request; the layout given back on free (`dealloc` takes the `Layout`,
//! `el_free` the `el_layout`, the same size and alignment), so neither side
//! needs a header; and no zero-size request ever reaches an implementation
//! (`GlobalAlloc::alloc` forbids it, `el_alloc` answers it itself).
//! `ElemLayout` is `Layout` one to one: two words, passed by value.
//!
//! What does not: `GlobalAlloc` must be `Sync`, and an `el_allocator` handle
//! carries no synchronization, so the arena sits behind a `Mutex`.

use std::alloc::{GlobalAlloc, Layout, System};
use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::{c_char, c_int, c_void};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr::null_mut;
use std::sync::Mutex;

// ---- The C contracts ------------------------------------------------------

pub type ElemStatus = i32;
pub const EL_OK: ElemStatus = 0;
pub const EL_ERR_NO_SPACE: ElemStatus = 4;

/// `el_layout`: a size and a power-of-two alignment, two words, by value.
/// Rust's `Layout` is not `repr(C)`, so it is converted field by field.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ElemLayout {
    pub size: usize,
    pub align: usize,
}

impl From<Layout> for ElemLayout {
    fn from(layout: Layout) -> Self {
        ElemLayout { size: layout.size(), align: layout.align() }
    }
}

impl ElemLayout {
    /// Back to a `Layout`. `None` only for a size Rust cannot describe
    /// (over `isize::MAX` once rounded up to the alignment); the alignment
    /// is a power of two by `el_layout`'s contract.
    pub fn to_layout(self) -> Option<Layout> {
        Layout::from_size_align(self.size, self.align).ok()
    }
}

#[repr(C)]
pub struct ElemAllocatorOps {
    pub alloc: unsafe extern "C" fn(*mut c_void, ElemLayout) -> *mut c_void,
    pub free: unsafe extern "C" fn(*mut c_void, *mut c_void, ElemLayout),
}

/// A handle: two pointers, passed by value, owning nothing.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct ElemAllocator {
    pub ctx: *mut c_void,
    pub ops: *const ElemAllocatorOps,
}

/// Public layout, so the caller provides the storage.
#[repr(C)]
pub struct ElemStackArena {
    base: *mut u8,
    cap: usize,
    used: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ElemWriter {
    ctx: *mut c_void,
    ops: *const c_void,
}

#[repr(C)]
pub struct ElemGrowWriter {
    allocator: ElemAllocator,
    buf: *mut u8,
    len: usize,
    cap: usize,
}

extern "C" {
    fn el_layout_make(size: usize, align: usize) -> ElemLayout;
    fn el_layout_array(elem: ElemLayout, count: usize, out: *mut ElemLayout) -> ElemStatus;
    fn el_alloc(allocator: ElemAllocator, layout: ElemLayout) -> *mut c_void;
    fn el_free(allocator: ElemAllocator, ptr: *mut c_void, layout: ElemLayout);
    fn el_stack_arena_init(arena: *mut ElemStackArena, storage: *mut c_void, cap: usize);
    fn el_stack_arena_as_allocator(arena: *mut ElemStackArena) -> ElemAllocator;
    fn el_grow_writer_init(gw: *mut ElemGrowWriter, allocator: ElemAllocator);
    fn el_grow_writer_as_writer(gw: *mut ElemGrowWriter) -> ElemWriter;
    fn el_grow_writer_data(gw: *const ElemGrowWriter) -> *const u8;
    fn el_grow_writer_len(gw: *const ElemGrowWriter) -> usize;
    fn el_grow_writer_deinit(gw: *mut ElemGrowWriter);
    fn el_write_all(writer: ElemWriter, data: *const c_void, len: usize, n: *mut usize)
        -> ElemStatus;
}

/// The application-provided assertion handler, for an Elemental configured
/// with ASSERT_HANDLER_APPLICATION. Unused, and harmless, otherwise.
#[no_mangle]
pub extern "C" fn el_assert_failed(_file: *const c_char, _line: c_int) -> ! {
    std::process::abort()
}

// ---- 1. A C allocator used from Rust --------------------------------------

/// `GlobalAlloc` over a C stack arena. The arena object and its storage live
/// as long as `ElGlobal`; a handle is made per call, under the lock.
pub struct ElGlobal {
    arena: Mutex<Box<ElemStackArena>>,
    _storage: Box<[u8]>,
}

impl ElGlobal {
    pub fn new(cap: usize) -> Self {
        let mut storage = vec![0u8; cap].into_boxed_slice();
        let mut arena = Box::new(ElemStackArena { base: null_mut(), cap: 0, used: 0 });
        unsafe { el_stack_arena_init(&mut *arena, storage.as_mut_ptr().cast(), cap) };
        ElGlobal { arena: Mutex::new(arena), _storage: storage }
    }
}

unsafe impl GlobalAlloc for ElGlobal {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        // Layout guarantees a nonzero power-of-two alignment; GlobalAlloc's
        // caller guarantees a nonzero size. Both are what el_alloc asks.
        let mut arena = self.arena.lock().unwrap_or_else(|e| e.into_inner());
        let handle = el_stack_arena_as_allocator(&mut **arena);
        el_alloc(handle, layout.into()).cast()
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        // The layout the block was allocated with: exactly el_free's contract.
        let mut arena = self.arena.lock().unwrap_or_else(|e| e.into_inner());
        let handle = el_stack_arena_as_allocator(&mut **arena);
        el_free(handle, ptr.cast(), layout.into());
    }
}

fn c_allocator_from_rust() {
    let global = ElGlobal::new(4096);
    unsafe {
        for &(size, align) in &[(1usize, 1usize), (24, 8), (100, 64), (7, 2)] {
            let layout = Layout::from_size_align(size, align).unwrap();
            let ptr = global.alloc(layout);
            assert!(!ptr.is_null(), "{size} bytes aligned to {align}");
            assert_eq!(ptr as usize % align, 0);
            std::ptr::write_bytes(ptr, 0x5A, size);
            // Freed in the order allocated: the stack arena takes back the
            // latest allocation, so each loop reuses the same storage.
            global.dealloc(ptr, layout);
        }
        // Exhaustion is null, as GlobalAlloc requires.
        let too_big = Layout::from_size_align(8192, 8).unwrap();
        assert!(global.alloc(too_big).is_null());

        // The two layout constructors agree with Rust's.
        assert_eq!(el_layout_make(24, 8), ElemLayout::from(Layout::new::<[u64; 3]>()));
        let mut array = ElemLayout { size: 0, align: 0 };
        let elem = el_layout_make(12, 4);
        assert_eq!(el_layout_array(elem, 10, &mut array), EL_OK);
        assert_eq!(array.to_layout(), Some(Layout::array::<[u32; 3]>(10).unwrap()));
        // An overflowing count is a status, and the output is a layout no
        // allocator can satisfy, which Rust's Layout also refuses.
        assert_eq!(el_layout_array(elem, usize::MAX, &mut array), EL_ERR_NO_SPACE);
        assert_eq!(array, ElemLayout { size: usize::MAX, align: 4 });
        assert_eq!(array.to_layout(), None);
    }
    println!("GlobalAlloc over el_stack_arena: aligned, reused, null when exhausted");
}

// ---- 2. A Rust allocator used from C --------------------------------------

/// Rust's system allocator as an `el_allocator`. It records every live
/// block with its layout, to show that what C frees is exactly what it
/// allocated: sized free needs no header on this side either.
pub struct RustAllocator {
    live: RefCell<HashMap<usize, Layout>>,
    allocs: usize,
}

impl RustAllocator {
    const OPS: ElemAllocatorOps =
        ElemAllocatorOps { alloc: Self::alloc_trampoline, free: Self::free_trampoline };

    pub fn new() -> Self {
        RustAllocator { live: RefCell::new(HashMap::new()), allocs: 0 }
    }

    /// The handle points at `self`, which must not move while it is in use.
    pub fn handle(&mut self) -> ElemAllocator {
        ElemAllocator { ctx: (self as *mut Self).cast(), ops: &Self::OPS }
    }

    unsafe extern "C" fn alloc_trampoline(ctx: *mut c_void, layout: ElemLayout) -> *mut c_void {
        // Guaranteed by el_alloc(): size >= 1, align a power of two. A size
        // Rust cannot describe (over isize::MAX once rounded) is a refusal.
        let this = &mut *ctx.cast::<Self>();
        let Some(layout) = layout.to_layout() else { return null_mut() };
        match catch_unwind(AssertUnwindSafe(|| {
            let ptr = System.alloc(layout);
            if !ptr.is_null() {
                this.live.borrow_mut().insert(ptr as usize, layout);
                this.allocs += 1;
            }
            ptr
        })) {
            Ok(ptr) => ptr.cast(),
            Err(_) => std::process::abort(), // nothing may unwind into C
        }
    }

    unsafe extern "C" fn free_trampoline(ctx: *mut c_void, ptr: *mut c_void, layout: ElemLayout) {
        let this = &mut *ctx.cast::<Self>();
        let recorded = this.live.borrow_mut().remove(&(ptr as usize));
        // Rebuilt from what C passed back, with no lookup needed: the
        // recorded layout is only here to prove they agree.
        let layout = Layout::from_size_align_unchecked(layout.size, layout.align);
        if recorded != Some(layout) {
            eprintln!("C freed {ptr:?} as {layout:?}, allocated as {recorded:?}");
            std::process::abort();
        }
        System.dealloc(ptr.cast(), layout);
    }
}

fn rust_allocator_from_c() {
    let mut rust = RustAllocator::new();
    let handle = rust.handle();
    let mut gw = ElemGrowWriter { allocator: handle, buf: null_mut(), len: 0, cap: 0 };
    let text: Vec<u8> = (0..10_000u32).map(|i| (i % 251) as u8).collect();

    unsafe {
        el_grow_writer_init(&mut gw, handle);
        let writer = el_grow_writer_as_writer(&mut gw);
        for chunk in text.chunks(97) {
            let status = el_write_all(writer, chunk.as_ptr().cast(), chunk.len(), null_mut());
            assert_eq!(status, EL_OK);
        }
        let len = el_grow_writer_len(&gw);
        let written = std::slice::from_raw_parts(el_grow_writer_data(&gw), len);
        assert_eq!(written, &text[..]);
        el_grow_writer_deinit(&mut gw);
    }
    assert!(rust.live.borrow().is_empty(), "C left a block allocated");
    assert!(rust.allocs > 1, "the writer grew more than once");
    println!(
        "el_grow_writer over Rust's allocator: {} bytes, {} allocations, all freed with their layouts",
        text.len(),
        rust.allocs
    );
}

fn main() {
    c_allocator_from_rust();
    rust_allocator_from_c();
}
