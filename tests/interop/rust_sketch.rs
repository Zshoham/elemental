//! Sketch: Elemental's io contracts from Rust. Compiled, linked against the C
//! static libraries, and run by `ctest -R interop.rust`.
//!
//! This is not a binding. It exists to check that the C contracts map onto
//! Rust's ownership and unwinding rules without awkwardness, in both
//! directions:
//!
//!   1. A C stream used from Rust (`MemReader` implements `std::io::Read`).
//!   2. A Rust object used from C (`WriterAdapter` hands any `std::io::Write`
//!      to C as an `el_writer`).
//!
//! The declarations are written by hand, as a real binding's generated ones
//! would look.

use std::ffi::{c_char, c_int, c_void, CStr};
use std::io::{self, Read, Write};
use std::marker::PhantomData;
use std::panic::{catch_unwind, AssertUnwindSafe};

// ---- The C contracts ------------------------------------------------------

/// `el_status` is `int32_t`, not a C enum, so it has one representation.
pub type ElemStatus = i32;
pub const EL_OK: ElemStatus = 0;
pub const EL_ERR_WOULD_BLOCK: ElemStatus = 1;
pub const EL_ERR_END_OF_STREAM: ElemStatus = 2;
pub const EL_ERR_IO: ElemStatus = 3;

#[repr(C)]
pub struct ElemReaderOps {
    pub read: unsafe extern "C" fn(*mut c_void, *mut c_void, usize, *mut usize) -> ElemStatus,
}

#[repr(C)]
pub struct ElemWriterOps {
    pub write: unsafe extern "C" fn(*mut c_void, *const c_void, usize, *mut usize) -> ElemStatus,
}

/// Handles are two pointers, passed by value. They own nothing.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct ElemReader {
    pub ctx: *mut c_void,
    pub ops: *const ElemReaderOps,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ElemWriter {
    pub ctx: *mut c_void,
    pub ops: *const ElemWriterOps,
}

/// Public layout, so the caller provides the storage.
#[repr(C)]
pub struct ElemMemReader {
    data: *const u8,
    len: usize,
    pos: usize,
}

extern "C" {
    fn el_read(reader: ElemReader, buf: *mut c_void, cap: usize, n: *mut usize) -> ElemStatus;
    fn el_write_all(
        writer: ElemWriter,
        data: *const c_void,
        len: usize,
        n: *mut usize,
    ) -> ElemStatus;
    fn el_mem_reader_init(mr: *mut ElemMemReader, data: *const c_void, len: usize);
    fn el_mem_reader_as_reader(mr: *mut ElemMemReader) -> ElemReader;
    fn el_status_name(status: ElemStatus) -> *const c_char;
}

/// The application-provided assertion handler, for an Elemental configured
/// with ASSERT_HANDLER_APPLICATION. Unused, and harmless, otherwise.
#[no_mangle]
pub extern "C" fn el_assert_failed(_file: *const c_char, _line: c_int) -> ! {
    std::process::abort()
}

fn status_name(status: ElemStatus) -> &'static str {
    // Never null, and the string has static storage duration.
    unsafe { CStr::from_ptr(el_status_name(status)) }
        .to_str()
        .unwrap_or("unknown")
}

// ---- 1. A C stream used from Rust -----------------------------------------

/// Owns the C object; borrows the bytes for `'a`.
///
/// The C contract says the bytes must outlive the reader: that is the
/// lifetime parameter. It also says the object may move while no handle made
/// from it is in use. A handle is made per call and never stored, so this
/// type needs no pinning and no heap allocation.
pub struct MemReader<'a> {
    raw: ElemMemReader,
    bytes: PhantomData<&'a [u8]>,
}

impl<'a> MemReader<'a> {
    pub fn new(bytes: &'a [u8]) -> Self {
        let mut raw = ElemMemReader { data: std::ptr::null(), len: 0, pos: 0 };
        unsafe { el_mem_reader_init(&mut raw, bytes.as_ptr().cast(), bytes.len()) };
        MemReader { raw, bytes: PhantomData }
    }
}

impl Read for MemReader<'_> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let mut n = 0usize;
        let status = unsafe {
            let handle = el_mem_reader_as_reader(&mut self.raw);
            el_read(handle, buf.as_mut_ptr().cast(), buf.len(), &mut n)
        };
        // "Progress or status, never both" makes this a total mapping.
        match status {
            EL_OK => Ok(n),
            EL_ERR_END_OF_STREAM => Ok(0),
            EL_ERR_WOULD_BLOCK => Err(io::ErrorKind::WouldBlock.into()),
            other => Err(io::Error::new(io::ErrorKind::Other, status_name(other))),
        }
    }
}

// ---- 2. A Rust object used from C -----------------------------------------

/// Lends a Rust writer to C.
///
/// C's contract for the operation: it must not unwind, and it reports
/// failure as a status. So a panic is caught here, and an `io::Error` that a
/// status cannot carry is kept for the Rust caller to pick up afterwards.
pub struct WriterAdapter<'a, W: Write> {
    inner: &'a mut W,
    error: Option<io::Error>,
    panicked: bool,
}

impl<'a, W: Write> WriterAdapter<'a, W> {
    /// One table per writer type, with static lifetime, as the contract asks.
    const OPS: ElemWriterOps = ElemWriterOps { write: Self::write_trampoline };

    pub fn new(inner: &'a mut W) -> Self {
        WriterAdapter { inner, error: None, panicked: false }
    }

    unsafe extern "C" fn write_trampoline(
        ctx: *mut c_void,
        data: *const c_void,
        len: usize,
        n: *mut usize,
    ) -> ElemStatus {
        // Guaranteed by el_write(): len >= 1, data and n are not null.
        let this = &mut *ctx.cast::<Self>();
        let bytes = std::slice::from_raw_parts(data.cast::<u8>(), len);

        match catch_unwind(AssertUnwindSafe(|| this.inner.write(bytes))) {
            // `Write::write` returning 0 for a non-empty buffer means the
            // writer can take no more.
            Ok(Ok(0)) => EL_ERR_END_OF_STREAM,
            Ok(Ok(count)) => {
                *n = count.min(len);
                EL_OK
            }
            Ok(Err(error))
                if matches!(
                    error.kind(),
                    io::ErrorKind::WouldBlock | io::ErrorKind::Interrupted
                ) =>
            {
                EL_ERR_WOULD_BLOCK
            }
            Ok(Err(error)) => {
                this.error = Some(error);
                EL_ERR_IO
            }
            Err(_panic) => {
                this.panicked = true;
                EL_ERR_IO
            }
        }
    }

    /// Have C's `el_write_all()` drive the Rust writer.
    ///
    /// The handle points at `self` and is used only inside this call, while
    /// `self` is exclusively borrowed and cannot move.
    pub fn write_all_through_c(&mut self, data: &[u8]) -> io::Result<()> {
        let handle = ElemWriter { ctx: (self as *mut Self).cast(), ops: &Self::OPS };
        let mut n = 0usize;
        let status = unsafe { el_write_all(handle, data.as_ptr().cast(), data.len(), &mut n) };

        if self.panicked {
            self.panicked = false;
            return Err(io::Error::new(io::ErrorKind::Other, "writer panicked"));
        }
        if let Some(error) = self.error.take() {
            return Err(error);
        }
        match status {
            EL_OK => Ok(()),
            EL_ERR_WOULD_BLOCK => Err(io::ErrorKind::WouldBlock.into()),
            EL_ERR_END_OF_STREAM => Err(io::ErrorKind::WriteZero.into()),
            other => Err(io::Error::new(io::ErrorKind::Other, status_name(other))),
        }
    }
}

// ---- The checks -----------------------------------------------------------

struct FailingWriter;
impl Write for FailingWriter {
    fn write(&mut self, _: &[u8]) -> io::Result<usize> {
        Err(io::Error::new(io::ErrorKind::PermissionDenied, "not today"))
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

struct PanickingWriter;
impl Write for PanickingWriter {
    fn write(&mut self, _: &[u8]) -> io::Result<usize> {
        panic!("a bug in the Rust writer")
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

/// Accepts at most three bytes per call, like a slow device.
struct TrickleWriter(Vec<u8>);
impl Write for TrickleWriter {
    fn write(&mut self, data: &[u8]) -> io::Result<usize> {
        let count = data.len().min(3);
        self.0.extend_from_slice(&data[..count]);
        Ok(count)
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

fn main() {
    // 1. Read a C memory stream through std::io::Read, after moving it.
    let message = b"elemental from Rust";
    let reader = MemReader::new(message);
    let mut moved = reader;
    let mut text = Vec::new();
    moved.read_to_end(&mut text).expect("memory streams do not fail");
    assert_eq!(text, message);

    // 2a. C drives a Rust writer, joining its short writes.
    let mut trickle = TrickleWriter(Vec::new());
    WriterAdapter::new(&mut trickle)
        .write_all_through_c(message)
        .expect("the trickle writer accepts everything eventually");
    assert_eq!(trickle.0, message);

    // 2b. A Rust error comes back as the same Rust error, not just a status.
    let mut failing = FailingWriter;
    let error = WriterAdapter::new(&mut failing).write_all_through_c(b"x").unwrap_err();
    assert_eq!(error.kind(), io::ErrorKind::PermissionDenied);

    // 2c. A panic does not unwind into C.
    std::panic::set_hook(Box::new(|_| {}));
    let mut panicking = PanickingWriter;
    let error = WriterAdapter::new(&mut panicking).write_all_through_c(b"x").unwrap_err();
    assert_eq!(error.to_string(), "writer panicked");
    let _ = std::panic::take_hook();

    assert_eq!(status_name(EL_ERR_IO), "io");
    println!("rust sketch ok");
}
