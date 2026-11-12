# alloc

The allocator interface, two arenas over caller-provided storage, a backend
over the C library's heap, and a contract suite with a checking allocator
for tests.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Targets | `elemental::alloc`, `elemental::alloc_contract`, `elemental::alloc_libc` |
| Depends on | `status`. Runtime: nothing. |
| Allocates | It is the interface. The arenas allocate from storage the caller passes in. |
| Kconfig | `ALLOC`, `ALLOC_LIBC` |

## What is here

| Piece | Kind | Header |
|---|---|---|
| `el_allocator`, `el_alloc`, `el_alloc_array`, `el_free` | Interface contract | [`elemental/alloc.h`](include/elemental/alloc.h) |
| `el_bump_arena`, `el_stack_arena` | Portable implementations | [`elemental/alloc_arena.h`](include/elemental/alloc_arena.h) |
| The allocator contract suite, and `el_alloc_checker` | GoogleTest tests and test support, shipped as source | [`elemental/alloc_contract.hpp`](contract/include/elemental/alloc_contract.hpp), fixture and checker in [`elemental/alloc_contract.h`](contract/include/elemental/alloc_contract.h) |
| `el_libc_allocator` | Platform backend | [`elemental/alloc_libc.h`](backends/libc/include/elemental/alloc_libc.h), [PORT.md](backends/libc/PORT.md) |

Start with `alloc.h`. The design and the examples that shaped it are in the
[proposal](../../docs/proposals/alloc.md); why free takes the size back is
[ADR 0012](../../docs/adr/0012-sized-free.md).

## Using it

Take an `el_allocator` as a parameter wherever you need dynamic storage,
keep it if you will free later, and free with the size and alignment you
allocated:

```c
reading *const items = el_alloc_array(a, cap, sizeof(reading), _Alignof(reading));
if (items == NULL) {
    return EL_ERR_NO_SPACE;
}
/* ... */
el_free(a, items, cap * sizeof(reading), _Alignof(reading));
```

### Failures and contract violations

The split follows [conventions.md](../../docs/conventions.md#arguments):
what a correct program can run into is reported, and what only a bug can
produce is a contract violation.

| Reported, never asserted | How |
|---|---|
| The allocator is exhausted | `el_alloc` and `el_alloc_array` return `NULL`; nothing changed |
| `count * size` overflows | `el_alloc_array` returns `NULL` without asking the allocator |
| The allocator cannot provide the alignment asked for | `NULL` |
| A function that allocates as part of other work runs out | `EL_ERR_NO_SPACE` ([ADR 0009](../../docs/adr/0009-capacity-status.md)) |

| Contract violation, asserted | Why it is not a status |
|---|---|
| A handle that was never made; a null arena | A calling bug |
| An alignment that is not a power of two | Alignments come from `_Alignof`, not from input |
| Arena storage that is null with a nonzero size | A calling bug |
| `el_free` of `NULL` with a nonzero size, or of the zero-size pointer with a nonzero size | A calling bug |
| Freeing memory the allocator did not provide, twice, or with a different size or alignment, where the allocator can check it cheaply (the arenas' range check) | The heap is already being corrupted; no caller action could recover, and a status from `el_free` would have nothing to tell it to do |
| An allocator returning a misaligned pointer | A broken backend |

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
alignment padding separated the allocations; otherwise the padding, and
anything below it, waits for the reset
([alloc_arena.h](include/elemental/alloc_arena.h)).

The growable writer that uses an allocator is
[`el_grow_writer`](../io/README.md#the-grow-writer-io_alloc), in `io`.

## Implementing an allocator

1. Define your state and the two operations of `el_allocator_ops`. The
   "called with" and "must" lists in `alloc.h` are the whole contract: no
   zero size, no bad alignment, and no null pointer reaches you, and your
   result's alignment is checked for you.
2. Put them in a `static const` table and write a function that returns a
   handle.
3. Write a fixture in C and instantiate the contract suite with it.
4. Write tests for what only your allocator promises.

[examples/backend_size_classes](../../examples/backend_size_classes) is a
complete allocator done this way, with no per-block header because free is
sized.

## The contract suite

`ElAllocContract` checks what the interface promises, against any
allocator a fixture describes:

```cpp
#include <elemental/alloc_contract.hpp>

#include "my_fixture.h"

INSTANTIATE_TEST_SUITE_P(MyAllocator, ElAllocContract, testing::Values(&my_fixture));
```

A fixture has two required functions and one optional capability,
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
| Bump arena | 11 | 2: free gives nothing back |
| Stack arena | 12 | 1: only the latest allocation is given back |
| libc heap | 8 | 5: no bound can be set, so neither the bound nor reclaiming can be observed |
| Size classes example | 13 | 0 |

Deliberately broken allocators were used to check the suite itself; the
results are in [docs/build.md](../../docs/build.md#what-is-verified).

### The checking allocator

`el_alloc_checker` wraps any allocator and stops the program, with a
message, on a free whose size or alignment differs from the allocation, a
double free, or a pointer it never handed out, and counts what is still
live. These are the mistakes sized free makes possible and no allocator can
check cheaply on its own. Use it in tests of anything that allocates; it
needs a hosted C library and links with `elemental::alloc_contract`.

## Tests

| Test | What it checks |
|---|---|
| `alloc.contract.arena.*` | The contract suite against both arenas. |
| `alloc.unit.*` | The zero-size pointer; the overflow check against the compiler's; arena placement, padding, exhaustion, and what each arena's free and reset do. |
| `alloc.violations.*` | Each documented contract violation stops the program, and the checking allocator stops each mismatch. Death tests, host only. |
| `alloc_libc.contract.*`, `alloc_libc.platform.*` | See [PORT.md](backends/libc/PORT.md). |

## Known gaps

Deferred until a consumer needs them: resize in place, pools, statistics
(such as an arena's high-water mark), and thread safety. Zeroed allocation
has no consumer.
