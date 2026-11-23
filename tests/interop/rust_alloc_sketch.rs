//! Sketch: Elemental's allocator contract next to Rust's. Compiled, linked
//! against the C static libraries, and run by `ctest -R interop.rust_alloc`.
//!
//! This is not a binding. It checks that `el_allocator` and Rust's
//! `GlobalAlloc` describe the same contract, in both directions:
//!
//!   1. A C allocator used from Rust: `ElGlobal` implements `GlobalAlloc`
//!      over an `el_stack_arena`.
//!   2. A Rust allocator used from C: `RustAllocator` hands Rust's global
//!      allocator to C as an `el_allocator`, with and without the optional
//!      `realloc` operation, and C's `el_grow_buf` grows in it, written
//!      through the memory endpoint `el_buf_as_writer`.
//!
//! What lines up: null on failure; a power-of-two alignment on every
//! request; the layout given back on free (`dealloc` takes the `Layout`,
//! `el_free` the `el_layout`, the same size and alignment), so neither side
//! needs a header; and no zero-size request ever reaches an implementation
//! (`GlobalAlloc::alloc` forbids it, `el_alloc` answers it itself). Changing
//! a size lines up too: `GlobalAlloc::realloc` may move, keeps the
//! alignment, and leaves the block alone when it returns null, as the
//! `realloc` operation and `el_realloc` do. `ElemLayout` is `Layout` one to
//! one: two words, passed by value.
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

/// `el_allocator_ops`. `realloc` is optional in C, so it is an `Option`: a
/// null pointer there is `None`, and `Option<unsafe extern "C" fn>` has the
/// layout of a C function pointer, so the struct is the one C reads.
#[repr(C)]
pub struct ElemAllocatorOps {
    pub alloc: unsafe extern "C" fn(*mut c_void, ElemLayout) -> *mut c_void,
    pub free: unsafe extern "C" fn(*mut c_void, *mut c_void, ElemLayout),
    pub realloc:
        Option<unsafe extern "C" fn(*mut c_void, *mut c_void, ElemLayout, usize) -> *mut c_void>,
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

/// `el_buf`: four words. `grow` is set by `el_grow_buf_init`; Rust never
/// calls it.
#[repr(C)]
pub struct ElemBuf {
    data: *mut u8,
    len: usize,
    cap: usize,
    grow: Option<unsafe extern "C" fn(*mut ElemBuf, usize) -> ElemStatus>,
}

/// `el_grow_buf`: the buffer first, then its allocator.
#[repr(C)]
pub struct ElemGrowBuf {
    buf: ElemBuf,
    allocator: ElemAllocator,
}

extern "C" {
    fn el_layout_make(size: usize, align: usize) -> ElemLayout;
    fn el_layout_array(elem: ElemLayout, count: usize, out: *mut ElemLayout) -> ElemStatus;
    fn el_alloc(allocator: ElemAllocator, layout: ElemLayout) -> *mut c_void;
    fn el_free(allocator: ElemAllocator, ptr: *mut c_void, layout: ElemLayout);
    fn el_realloc(
        allocator: ElemAllocator,
        ptr: *mut c_void,
        layout: ElemLayout,
        new_size: usize,
    ) -> *mut c_void;
    fn el_stack_arena_init(arena: *mut ElemStackArena, storage: *mut c_void, cap: usize);
    fn el_stack_arena_as_allocator(arena: *mut ElemStackArena) -> ElemAllocator;
    fn el_grow_buf_init(g: *mut ElemGrowBuf, allocator: ElemAllocator) -> *mut ElemBuf;
    fn el_grow_buf_deinit(g: *mut ElemGrowBuf);
    fn el_buf_as_writer(b: *mut ElemBuf) -> ElemWriter;
    fn el_buf_data(b: *const ElemBuf) -> *const u8;
    fn el_buf_len(b: *const ElemBuf) -> usize;
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

    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        // GlobalAlloc's caller guarantees the layout the block has and a
        // nonzero new size: what el_realloc asks. Null leaves the block as
        // it was, which is what GlobalAlloc::realloc promises too.
        let mut arena = self.arena.lock().unwrap_or_else(|e| e.into_inner());
        let handle = el_stack_arena_as_allocator(&mut **arena);
        el_realloc(handle, ptr.cast(), layout.into(), new_size).cast()
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

        // GlobalAlloc::realloc over el_realloc: the latest allocation grows
        // where it is, and a request the arena cannot meet leaves the block
        // as it was.
        let small = Layout::from_size_align(16, 8).unwrap();
        let big = Layout::from_size_align(64, 8).unwrap();
        let ptr = global.alloc(small);
        assert!(!ptr.is_null());
        for i in 0..16 {
            *ptr.add(i) = i as u8;
        }
        let grown = global.realloc(ptr, small, 64);
        assert_eq!(grown, ptr, "the latest allocation grows in place");
        for i in 0..16 {
            assert_eq!(*grown.add(i), i as u8);
        }
        assert!(global.realloc(grown, big, 8192).is_null());
        for i in 0..16 {
            assert_eq!(*grown.add(i), i as u8, "a refused realloc changed the block");
        }
        global.dealloc(grown, big);
    }
    println!("GlobalAlloc over el_stack_arena: aligned, reused, resized, null when exhausted");
}

// ---- 2. A Rust allocator used from C --------------------------------------

/// Rust's system allocator as an `el_allocator`. It records every live
/// block with its layout, to show that what C frees, or resizes, is exactly
/// what it allocated: sized free needs no header on this side either.
pub struct RustAllocator {
    live: RefCell<HashMap<usize, Layout>>,
    ops: &'static ElemAllocatorOps,
    allocs: usize,
    reallocs: usize,
}

impl RustAllocator {
    /// The `realloc` field null: `el_realloc` allocates, copies and frees.
    const OPS: ElemAllocatorOps = ElemAllocatorOps {
        alloc: Self::alloc_trampoline,
        free: Self::free_trampoline,
        realloc: None,
    };
    /// The field present, answered by `GlobalAlloc::realloc`.
    const OPS_WITH_REALLOC: ElemAllocatorOps = ElemAllocatorOps {
        alloc: Self::alloc_trampoline,
        free: Self::free_trampoline,
        realloc: Some(Self::realloc_trampoline),
    };

    pub fn new(with_realloc: bool) -> Self {
        RustAllocator {
            live: RefCell::new(HashMap::new()),
            ops: if with_realloc { &Self::OPS_WITH_REALLOC } else { &Self::OPS },
            allocs: 0,
            reallocs: 0,
        }
    }

    /// The handle points at `self`, which must not move while it is in use.
    pub fn handle(&mut self) -> ElemAllocator {
        ElemAllocator { ctx: (self as *mut Self).cast(), ops: self.ops }
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

    /// `GlobalAlloc::realloc` is the counterpart of the `realloc` operation:
    /// it may move, and leaves the block alone when it returns null. It
    /// keeps the alignment, which is the layout's own, so there is no
    /// alignment to refuse here.
    unsafe extern "C" fn realloc_trampoline(
        ctx: *mut c_void,
        ptr: *mut c_void,
        layout: ElemLayout,
        new_size: usize,
    ) -> *mut c_void {
        // Guaranteed by el_realloc(): ptr and layout are a live allocation's,
        // and new_size >= 1.
        let this = &mut *ctx.cast::<Self>();
        let layout = Layout::from_size_align_unchecked(layout.size, layout.align);
        let recorded = this.live.borrow().get(&(ptr as usize)).copied();
        if recorded != Some(layout) {
            eprintln!("C resized {ptr:?} as {layout:?}, allocated as {recorded:?}");
            std::process::abort();
        }
        // GlobalAlloc::realloc needs the new size to fit a Layout: null if it
        // does not, so that el_realloc tries the fallback and fails there.
        let Ok(resized) = Layout::from_size_align(new_size, layout.align()) else {
            return null_mut();
        };
        match catch_unwind(AssertUnwindSafe(|| {
            let moved = System.realloc(ptr.cast(), layout, new_size);
            if !moved.is_null() {
                let mut live = this.live.borrow_mut();
                live.remove(&(ptr as usize));
                live.insert(moved as usize, resized);
                this.reallocs += 1;
            }
            moved
        })) {
            Ok(moved) => moved.cast(),
            Err(_) => std::process::abort(), // nothing may unwind into C
        }
    }
}

fn rust_allocator_from_c(with_realloc: bool) {
    let mut rust = RustAllocator::new(with_realloc);
    let handle = rust.handle();
    // Initialized by el_grow_buf_init; the zeros only give Rust a value.
    let mut grown = ElemGrowBuf {
        buf: ElemBuf { data: null_mut(), len: 0, cap: 0, grow: None },
        allocator: handle,
    };
    let text: Vec<u8> = (0..10_000u32).map(|i| (i % 251) as u8).collect();

    unsafe {
        // `grown` stays put while `buf` is in use: it is not moved below.
        let buf = el_grow_buf_init(&mut grown, handle);
        let writer = el_buf_as_writer(buf);
        for chunk in text.chunks(97) {
            let status = el_write_all(writer, chunk.as_ptr().cast(), chunk.len(), null_mut());
            assert_eq!(status, EL_OK);
        }
        let len = el_buf_len(buf);
        let written = std::slice::from_raw_parts(el_buf_data(buf), len);
        assert_eq!(written, &text[..]);
        el_grow_buf_deinit(&mut grown);
    }
    assert!(rust.live.borrow().is_empty(), "C left a block allocated");
    if with_realloc {
        // The first growth is an allocation; every later one is the
        // operation's, which answered, so nothing was allocated again.
        assert_eq!(rust.allocs, 1, "el_realloc allocated although the operation answered");
        assert!(rust.reallocs > 1, "the buffer grew more than once");
    } else {
        // The field is null: every growth is an allocation, a copy, a free.
        assert_eq!(rust.reallocs, 0);
        assert!(rust.allocs > 1, "the buffer grew more than once");
    }
    println!(
        "el_grow_buf over Rust's allocator, realloc operation {}: {} bytes, {} allocations, \
         {} reallocations, all freed with their layouts",
        if with_realloc { "present" } else { "null" },
        text.len(),
        rust.allocs,
        rust.reallocs
    );
}

fn main() {
    c_allocator_from_rust();
    // The `realloc` field null: C allocates, copies and frees.
    rust_allocator_from_c(false);
    // The field present: C asks Rust's `GlobalAlloc::realloc`.
    rust_allocator_from_c(true);
}
