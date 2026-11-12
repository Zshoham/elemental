# 0013. An allocation is described by a layout

Status: Accepted (2026-11-13). Decided by the project owner. Refines
[ADR 0012](0012-sized-free.md): free still takes back what the allocation
was made with; what it takes back is now one value.

## Context

ADR 0012 made `el_free` take the size and alignment. Every call site then
carried two numbers beside a pointer, in two places: `el_alloc(a, size,
align)` and `el_free(a, ptr, size, align)`. For an array it was worse, because
the size at free was `count * size`, a product the caller had to recompute
and could get wrong. `el_alloc_array` did the overflow check at allocation
and left the caller to repeat the multiplication at free, where it "cannot
overflow" only by an argument in a comment. The hazard ADR 0012 recorded,
freeing with the length instead of the capacity, comes from the same place:
the size is a loose number that has to be remembered separately from the
alignment that goes with it.

Rust's `Layout`, Pigweed's `Layout`, and Zig's `std.mem.Alignment` plus a
length all put the pair in one value. Rust's `Layout::array` is also where
Rust's allocation arithmetic is checked.

## Decision

```c
typedef struct el_layout { size_t size; size_t align; /* a power of two */ } el_layout;

el_layout el_layout_make(size_t size, size_t align);
#define EL_LAYOUT_OF(T) el_layout_make(sizeof(T), _Alignof(T))
el_status el_layout_array(el_layout elem, size_t count, el_layout *out);

void *el_alloc(el_allocator a, el_layout layout);
void  el_free(el_allocator a, void *ptr, el_layout layout);
```

and the same `el_layout` in `el_allocator_ops.alloc` and `.free`.

- `el_alloc_array` is removed. `el_layout_array` followed by `el_alloc`
  replaces it, and the caller then holds the one value it frees with.
- `el_layout_array` reports `count * elem.size` overflowing as
  `EL_ERR_NO_SPACE`, because the count may come from input, and leaves `*out`
  unchanged. The check is the half-split multiply `el_alloc_array` had: no
  division and no double-width multiply, so no compiler helper on ARMv6-M.
- An alignment that is not a power of two is a contract violation, caught
  where the layout is built (`el_layout_make`) and again where it is used
  (`el_alloc`, `el_free`, `el_layout_array`), because the fields are public
  and a layout can be written by hand.
- Everything else of ADR 0012 and the allocation proposal stands: zero-size
  requests never reach an operation, exhaustion is `NULL`, a mismatched free
  is a violation where an allocator can see it.
- `EL_LAYOUT_OF` is a macro because only a macro can take a type. It does
  nothing `el_layout_make` does not, so the function is the real entry point
  (ABI rule 6). In C++ spell the same value `el_layout_make(sizeof(T),
  alignof(T))`: `_Alignof` is a C11 keyword.
- ABI rule 8 is widened from "only handles are passed by value" to "handles
  and `el_layout`": two words, trivially copyable, no `bool`, enum, or
  bit-field. A two-word struct passed by value goes in registers on the AAPCS
  and on System V x86-64, so `el_alloc(a, layout)` passes the same four words
  the old signature did. A two-word result comes back in registers on x86-64
  and through a caller-supplied buffer on 32-bit Arm, as `el_allocator`
  results already do.

Rust's `GlobalAlloc` now maps one to one: `Layout` is `el_layout`, field for
field (`tests/interop/rust_alloc_sketch.rs`).

## Cost

- **A function call to build a layout.** `el_layout_make` is out of line,
  because it asserts, and assertions are configuration that a public header
  cannot see ([ADR 0004](0004-private-configuration.md)). So `EL_LAYOUT_OF(T)`
  is a call, not a compile-time constant, and it cannot initialize a static.
  Code that allocates the same type often builds the layout once and keeps
  it. Measured on `mps2/an385` (Zephyr SDK GCC 14.3.0, size-optimized, trap
  configuration): `alloc` grew from 478 to 568 bytes of text (`el_layout_make`
  and `el_layout_array` are new code, and the arenas take a layout), and the
  grow writer from 332 to 378 (it builds a layout at three sites). The
  call itself is cheap next to an allocation.
- **Two steps for an array** where `el_alloc_array` was one. In return the
  overflow is a status the caller can tell from exhaustion, and the
  multiplication is done once.
- **`el_layout_array` leaves `*out` unchanged on failure**, an exception to
  "output parameters are always written" (conventions, Errors rule 4). A
  zeroed layout would be a valid layout (size 0) that a caller might go on to
  use; leaving the caller's value untouched makes the misuse visible in the
  caller's own variable. The exception is stated in the header and in
  conventions.
- **The fields are public**, so `(el_layout){8, 3}` compiles. `el_alloc` and
  `el_free` assert on it; with assertions compiled out it is undefined
  behavior, as every other precondition is.
- **A third kind of by-value struct** in the contract, which rule 8 had kept
  to handles. It is two words and nothing an ABI could pass differently, but a
  language binding must declare it `repr(C)` and pass it by value.

## Revisit when

- Measured code size or time on a small target shows `el_layout_make`'s call
  matters; the answer would be a public inline form, with the assertion
  compiled by the caller's configuration, which ADR 0004 forbids today.
- A consumer's arrays are built far more often than allocated, so that
  `el_layout_array`'s status is noise; a convenience in layout form
  (`el_alloc_array(a, elem, count, &layout)`) would then be worth its name.
- An allocator needs more in a layout than size and alignment (a lifetime
  class, an arena tag). It would be a new type, not a wider `el_layout`.
