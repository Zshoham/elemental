# 0012. Free takes the size and alignment back

Status: Accepted (2026-11-12). Decided by the project owner, on the
evidence of the [allocation proposal](../proposals/alloc.md).

## Context

An allocator interface has to say what freeing needs. Either `free` takes
only the pointer, as `free()` and Pigweed's `Deallocator::Deallocate(void *)`
do, and an allocator that must know a block's size keeps it in a header; or
`free` is also given the size and alignment the block was allocated with, as
Zig's `Allocator.free`, Rust's `GlobalAlloc::dealloc(ptr, layout)`, and C23's
`free_sized`/`free_aligned_sized` are. The choice reaches every call site and
every allocator, so it is expensive to change, and no module in the roadmap
consumes an allocator before async discovery. The proposal's three
mandatory examples were written to decide it.

## Decision

`el_free(allocator, ptr, size, align)`. The size and alignment must equal
those of the allocation (for `el_alloc_array`, `count * size`), and an
allocator may rely on them. A mismatch is a contract violation, checked
where an allocator can do so cheaply (the arenas check that the range is
theirs) and otherwise undefined, as for `free_sized`.

What the examples showed (proposal, "Sized free: the evidence"):

| Example | Cost of sized free |
|---|---|
| A string, kept as pointer and length | None: the length is stored anyway. A terminated scratch copy has its size beside both calls. |
| A growable buffer | One hazard: freeing with the length instead of the capacity. |
| An object handed across an API | None to the consumer: the library's destroy function frees, using a count it stores anyway. A buffer that the caller would free with a size the library dictates was avoided by writing into a caller-supplied `el_writer`. |

What decided it is an asymmetry. A sized contract can be implemented over
any pointer-only allocator (`malloc`, Zephyr's `sys_heap`) by ignoring two
arguments, as `alloc_libc` does. A pointer-only contract can be implemented
over a sized one (size classes, pools, Rust's allocator) only by adding a
header to every block. The `size_classes` example allocator has no header,
because free computes the block's class from the size and alignment. Rust's
`GlobalAlloc` maps onto `el_allocator` one to one in both directions
(`tests/interop/rust_alloc_sketch.rs`).

The hazard is met in tests: the checking allocator in
`elemental::alloc_contract` stops the program on a free whose size or
alignment does not match the allocation, and the grow writer's tests (`io`, with
`IO_ALLOC`) run through it.

## Cost

- Every caller must know, or store, the size it allocated. For a buffer
  that is its capacity, not its length.
- A wrong size is not detected at run time by allocators that ignore it
  (the libc backend), only by the checking allocator in tests.
- An API that hands the caller a buffer to free makes its exact size part of
  the API. The examples avoided that shape; one that cannot will have to
  document the size.
- Freeing a block that came from a pointer-only source (a C library that
  returns `malloc`ed memory) is not this interface's job.

## Revisit when

- The checking allocator, run over consumers that were not written to
  demonstrate allocation (async or IPC discovery, applications), finds size
  mismatches often enough to suggest the cost was misjudged.
- An allocator interface Elemental must interoperate with requires
  pointer-only free and a header turns out to be unacceptable there.
