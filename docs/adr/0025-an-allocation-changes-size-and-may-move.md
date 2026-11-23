# 0025. An allocation changes size through one optional operation that may move it

Status: Accepted (2026-11-23). Decided by the project owner in the review
of the [seams proposal](../proposals/seams.md#alloc-changing-an-allocations-size),
over the proposal's first draft, an operation that only resized in place.
It reverses the [alloc proposal](../proposals/alloc.md)'s deferral of
resizing.

## Context

`el_grow_buf` grows by allocating new storage, copying, and freeing the
old. A bump arena reclaims nothing on free, so a buffer grown to n bytes
used about 2n of it. *Measured* at 7768415: a 60-byte message needed a
112-byte bump arena (16 + 32 + 64), where growing the arena's latest
allocation in place needs 64.

An operation table only grows at a breaking release once a module is
stable ([conventions.md](../conventions.md#abi), rule 4), so the shape
chosen now is likely the one kept.

- Rust's allocator API had `grow_in_place` and `shrink_in_place`
  ([1.43 `AllocRef`](https://doc.rust-lang.org/1.43.0/std/alloc/trait.AllocRef.html))
  and replaced them with `grow` and `shrink`, which may move
  ([`Allocator`](https://doc.rust-lang.org/std/alloc/trait.Allocator.html)).
- Zig's allocator table has an in-place `resize` and, since 0.14, `remap`,
  which may move and returns null for "copy it yourself"
  ([`Allocator.zig`](https://raw.githubusercontent.com/ziglang/zig/0.14.1/lib/std/mem/Allocator.zig)).
- C has no in-place `realloc`, so an in-place operation leaves the C
  library's allocator out, and a second table change would be needed to
  let it use `realloc()`.

## Decision

- **`el_allocator_ops` gains one optional operation, `realloc`**, last in
  the table, so tables written with designated initializers leave it null.
  It changes an allocation's size and may move it. It returns the
  allocation's address, or null with nothing changed, which means "no
  better than allocate, copy and free", not only "no memory".
- **`el_realloc()` is the one front function.** It asks the operation
  first and, without one or on null, allocates, copies and frees. A size of
  0 at either end goes through `el_alloc` and `el_free`, never to the
  operation.
- After a success the allocation is the returned pointer with the new size
  and the old alignment: that is what `el_free` and the `free` operation
  are given from then on. As after C's `realloc()`, the caller uses only
  the returned pointer, even when it is the old address.
- The arenas grow and shrink their latest allocation in place; the C
  library's allocator answers with `realloc()` up to `max_align_t`;
  `el_grow_buf` grows through `el_realloc`.

## Cost

- One more pointer in every allocator's table, in read-only data.
- An allocator that is out of memory is asked twice: once through the
  operation, once by the fallback.
- An implementation must not return null after changing anything, which
  the contract suite checks and which a C `realloc()` already guarantees.

## Revisit when

- A consumer needs to know whether an allocation moved without moving it
  (an object other code points into): then an in-place query, as Zig's
  `resize`, is worth a second operation.
