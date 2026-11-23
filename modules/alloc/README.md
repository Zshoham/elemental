# alloc

The allocator interface, two arenas over caller-provided storage, a backend
over the C library's heap, and a contract suite with a checking allocator
for tests.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Targets | `elemental::alloc`, `elemental::alloc_contract`, `elemental::alloc_libc` |
| Depends on | `status`. Runtime: `memcpy`, for `el_realloc`'s copy, which the compiler requires of every environment. |
| Allocates | It is the interface. The arenas allocate from storage the caller passes in. |
| Kconfig | `ALLOC`, `ALLOC_LIBC` |

## What is here

| Piece | Kind | Header |
|---|---|---|
| `el_layout`, `el_layout_make`, `EL_LAYOUT_OF`, `el_layout_array`, `el_allocator`, `el_alloc`, `el_free`, `el_realloc` | Interface contract | [`elemental/alloc.h`](include/elemental/alloc.h) |
| `el_bump_arena`, `el_stack_arena` | Portable implementations | [`elemental/alloc_arena.h`](include/elemental/alloc_arena.h) |
| The allocator contract suite, and `el_alloc_checker` | GoogleTest tests and test support, shipped as source | [`elemental/alloc_contract.hpp`](contract/include/elemental/alloc_contract.hpp), fixture and checker in [`elemental/alloc_contract.h`](contract/include/elemental/alloc_contract.h) |
| `el_libc_allocator` | Platform backend | [`elemental/alloc_libc.h`](backends/libc/include/elemental/alloc_libc.h), [PORT.md](backends/libc/PORT.md) |

Start with `alloc.h`. The design and the examples that shaped it are in the
[proposal](../../docs/proposals/alloc.md); why free takes the size back is
[ADR 0012](../../docs/adr/0012-sized-free.md), and why the size and alignment
are one `el_layout` is [ADR 0013](../../docs/adr/0013-allocation-layout.md).

## Using it

Take an `el_allocator` as a parameter wherever you need dynamic storage,
keep it if you will free later, and free with the layout you allocated:

```c
el_layout items_layout;
if (el_layout_array(EL_LAYOUT_OF(reading), cap, &items_layout) != EL_OK) {
    return EL_ERR_NO_SPACE; /* cap * sizeof(reading) does not fit in a size_t */
}
reading *const items = el_alloc(a, items_layout);
if (items == NULL) {
    return EL_ERR_NO_SPACE;
}
/* ... */
el_free(a, items, items_layout);
```

One object is `el_alloc(a, EL_LAYOUT_OF(session))`; a byte buffer of `n` bytes
is `el_layout_make(n, 1)`. In C++ write `el_layout_make(sizeof(T), alignof(T))`
for `EL_LAYOUT_OF(T)`, which needs C's `_Alignof`.

### Failures and contract violations

The split follows [conventions.md](../../docs/conventions.md#arguments):
what a correct program can run into is reported, and what only a bug can
produce is a contract violation.

| Reported, never asserted | How |
|---|---|
| The allocator is exhausted | `el_alloc` returns `NULL`; nothing changed |
| `count * size` overflows | `el_layout_array` returns `EL_ERR_NO_SPACE` and sets `*out` to a size no allocator can satisfy; no allocator is asked |
| The allocator cannot provide the alignment asked for | `NULL` |
| A function that allocates as part of other work runs out | `EL_ERR_NO_SPACE` ([ADR 0009](../../docs/adr/0009-capacity-status.md)) |
| `el_realloc` cannot change the size | `NULL`; nothing changed, and the allocation is still at the old pointer with the old layout |

| Contract violation, asserted | Why it is not a status |
|---|---|
| A handle that was never made; a null arena | A calling bug |
| A layout whose alignment is not a power of two (`el_layout_make`, `el_layout_array`, `el_alloc`, `el_free`) | Alignments come from `_Alignof`, not from input |
| A null `out` to `el_layout_array` | A calling bug |
| Arena storage that is null with a nonzero size | A calling bug |
| `el_free` or `el_realloc` of `NULL` with a layout of nonzero size, or of the zero-size pointer with a layout of nonzero size (for `el_realloc`, only when the new size differs: a call to the same size returns the pointer before any check) | A calling bug |
| Freeing memory the allocator did not provide, twice, or with a different layout, where the allocator can check it cheaply (the arenas' range check) | The heap is already being corrupted; no caller action could recover, and a status from `el_free` would have nothing to tell it to do |
| An allocator returning a misaligned pointer, from `alloc` or from `realloc` | A broken backend |

What a violation does is the build's choice (`ASSERT`, `ASSERT_HANDLER_*`
in [build.md](../../docs/build.md#the-options)): trap in place, call the
application's handler (log and reset, say), or, with assertions compiled
out, no check at all. It never returns into the failed operation.

### Choosing an allocator

Which allocator a component gets is the application's choice:

| Allocator | Free gives back | Reset | For |
|---|---|---|---|
| `el_bump_arena` | nothing | everything | Storage for one request, frame, or message, released at once |
| `el_stack_arena` | the most recent allocation | everything | Scratch freed in reverse order |
| `el_libc_allocator()` | the block | — | Long-lived objects in a hosted program |

Over the stack arena, frees in reverse order give back everything when no
alignment padding separated the allocations and none was shrunk while it
was not the latest; otherwise the padding, or the shrunk allocation, and
anything below it, waits for the reset. Two 16-byte allocations, the
first shrunk to 8: after the second is freed the top is at 16, and the
first, which now ends at 8, is not recognized
([alloc_arena.h](include/elemental/alloc_arena.h)).

### Changing an allocation's size

`el_realloc(a, ptr, layout, new_size)` returns the allocation at `ptr`
with `new_size` bytes, at `ptr` or moved, its first `min(old, new)` bytes
kept. From then on only the returned pointer is used, even if it is the
old address (pointers into the allocation are derived from it again), and
it is freed with `el_layout_make(new_size, layout.align)`. `NULL` means it
could not be done and nothing changed: `ptr` and `layout` still name the
allocation. It asks the allocator's `realloc` operation first and
otherwise allocates, copies and frees. The arenas extend their latest
allocation in place, so an `el_grow_buf` over one wastes nothing while
every growth was of the arena's latest allocation; the libc backend calls
`realloc()`:

```c
if (len > SIZE_MAX / 2) {
    return EL_ERR_NO_SPACE; /* 2 * len does not fit in a size_t */
}
const size_t doubled = 2 * len;
uint8_t *const bigger = el_realloc(a, bytes, el_layout_make(len, 1), doubled);
if (bigger == NULL) {
    return EL_ERR_NO_SPACE; /* bytes is still valid, with its length of len */
}
bytes = bigger; /* freed with el_layout_make(doubled, 1) */
```

The growable buffer that uses an allocator is
[`el_grow_buf`](../io/README.md#buffers-io_buf-io_alloc), in `io`.

## Implementing an allocator

1. Define your state and the two operations of `el_allocator_ops`, and, if
   you can, the optional third. The
   "called with" and "must" lists in `alloc.h` are the whole contract: no
   zero-size layout, no bad alignment, and no null pointer reaches you, and
   your result's alignment is checked for you.
2. Put them in a `static const` table and write a function that returns a
   handle.
3. Write a fixture in C and instantiate the contract suite with it.
4. Write tests for what only your allocator promises.

[examples/backend_size_classes](../../examples/backend_size_classes) is a
complete allocator done this way, with no per-block header because free is
given the layout.

The third operation, `realloc`, is optional: leave it null if you can do no
better than allocate, copy and free, which `el_realloc` then does for you.
Answer only what you do better (extend a block where it is, as the arenas do
for their latest allocation), and return `NULL`, changing nothing, for the
rest. The example answers for a block that stays in its class.

## The contract suite

`ElAllocContract` checks what the interface promises, against any
allocator a fixture describes:

```cpp
#include <elemental/alloc_contract.hpp>

#include "my_fixture.h"

INSTANTIATE_TEST_SUITE_P(MyAllocator, ElAllocContract, testing::Values(&my_fixture));
```

A fixture has one required function, `allocator`, an optional `reset`, and
one optional capability,
**bounded**: an allocator that never holds more than a given number of
bytes. With it the fixture states what freeing gives back, at one of three
levels, because "reclaims on free" is not one property:

| `reclaims` | Promise | Claimed by |
|---|---|---|
| `EL_ALLOC_CONTRACT_RECLAIMS_NOTHING` | Nothing comes back until the allocator is reset or ended | Bump arena |
| `EL_ALLOC_CONTRACT_RECLAIMS_LATEST` | Freeing the latest allocation lets the same request succeed again, indefinitely | Stack arena |
| `EL_ALLOC_CONTRACT_RECLAIMS_ANY` | Freeing in any order makes the storage reusable | Size classes example; `ANY` implies `LATEST` |

The stack arena claims `LATEST` and not `ANY`: it recognizes only the
allocation that ends at its top, so a free out of order gives nothing back.
The suite's any-order test would fail against it.

| Implementation | Tests run | Skipped, and why |
|---|---|---|
| Bump arena | 23 | 2: free gives nothing back |
| Stack arena | 24 | 1: only the latest allocation is given back |
| Stack arena behind the checking allocator | 24 | 1: as the stack arena |
| Stack arena without a `realloc` operation | 22 | 3: as the stack arena, and the two tests of the operation itself |
| libc heap | 18 | 7: no bound can be set, so neither the bound nor reclaiming can be observed, nor a bounded resize |
| Size classes example | 25 | 0 |

Deliberately broken allocators were used to check the suite itself; the
results are in [docs/build.md](../../docs/build.md#what-is-verified).

### The checking allocator

`el_alloc_checker` wraps any allocator and stops the program, with a
message, on a free or a resize whose layout differs from the allocation, a
double free, or a pointer it never handed out, and counts what is still
live. After a resize it knows the new pointer and layout, so a later free
with the old ones stops the program. These are the mistakes sized free makes
possible and no allocator can check cheaply on its own. It sees only the
calls that reach its operations: `el_realloc` to the size the layout
already has returns the pointer, and from a layout of size 0 allocates,
before the checker is asked, so a wrong pointer or layout there is caught
only when that pointer is next freed or resized. Use it in tests of
anything that allocates; it needs a hosted C library and links with
`elemental::alloc_contract`.

## Tests

| Test | What it checks |
|---|---|
| `alloc.contract.arena.*` | The contract suite against both arenas, the stack arena behind the checking allocator, and the stack arena behind an allocator that leaves `realloc` null. |
| `alloc.unit.*` | The zero-size pointer; `el_layout_make`, `EL_LAYOUT_OF`, and `el_layout_array`, including its overflow check against the compiler's; arena placement, padding, exhaustion, and what each arena's free, reset and resize do (the latest allocation grows in place and gives its tail back; any other shrinks in place and is moved to grow). |
| `alloc.violations.*` | Each documented contract violation stops the program, `el_realloc`'s included, and the checking allocator stops each mismatch, a free or resize after a resize with the old pointer or size too. Death tests, host only. |
| `alloc_libc.contract.*`, `alloc_libc.platform.*` | See [PORT.md](backends/libc/PORT.md). |

## Known gaps

Deferred until a consumer needs them: pools, statistics (such as an arena's
high-water mark), and thread safety. Zeroed allocation has no consumer.
