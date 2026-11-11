# Port: alloc on the C library heap

This document follows the template in
[docs/process/porting.md](../../../../docs/process/porting.md).

| | |
|---|---|
| Interface | `el_allocator` ([alloc.h](../../include/elemental/alloc.h)) |
| Platform facility | `malloc`, `aligned_alloc`, `free` |
| Target | `elemental::alloc_libc` |
| Header | [`elemental/alloc_libc.h`](include/elemental/alloc_libc.h) |
| Stability | Experimental |
| Verification level | **Tested on a host**: Linux x86-64, glibc only |

## Capabilities

| Interface behavior | Supported | Notes |
|---|---|---|
| Any power-of-two alignment | Yes | Up to `max_align_t` through `malloc`; above it through `aligned_alloc`, with the size rounded up to a multiple of the alignment. Whatever `aligned_alloc` refuses is `NULL`. |
| Exhaustion as `NULL` | Yes | Whatever the C library refuses, and a size that overflows when rounded up. |
| Bounded | No | The heap has no bound this backend sets. |
| Reclaims on free | Yes | `free()`. Not observable by the contract suite without a bound. |
| Uses the size and alignment at free | No | They are accepted and ignored. |

## Unsupported semantics

- **Does not detect a wrong size or alignment at free**, nor a foreign or
  double free beyond what the C library itself catches. Use the checking
  allocator in tests.
- **Does not use C23 `free_sized` or `free_aligned_sized`.** The baseline is
  C11. With sized free the backend knows the alignment at free, so it could
  choose between them correctly, if a measurement ever asks for it.
- No bound, no statistics, no reset.

## Toolchain and runtime dependencies

A hosted C11 library with `aligned_alloc`. Uses `malloc`, `aligned_alloc`,
and `free`, nothing else.

## Configuration

`ALLOC_LIBC`, on by default where CMake finds `<unistd.h>`
(`PLATFORM_POSIX`), which stands in for "a hosted C library": Linux is the
only host Elemental supports. The Zephyr test image turns it off
([tests/zephyr/elemental_defconfig](../../../../tests/zephyr/elemental_defconfig)).

## Concurrency and context

The handle has no state. It may be used from several threads at once to the
extent the C library allows (glibc does). Not for interrupt handlers. May
block inside the C library.

## Verification

| Test | Kind | What it checks |
|---|---|---|
| `alloc_libc.contract.*` | Shared contract suite | Interface semantics. |
| `alloc_libc.platform.*` | Backend-specific | Alignments from twice `max_align_t` to 64 KiB, at sizes around each; the rounding overflow returns `NULL`; the handle is stateless. |

Skipped contract tests: 5, every bounded and reclaiming test, because the
heap cannot be given a bound.

Run with GCC 16.2.1 and Clang 23.1.4, and under AddressSanitizer and
UndefinedBehaviorSanitizer (with `allocator_may_return_null=1`, so that a
refused request returns `NULL` instead of stopping the program), on Linux
x86-64 with glibc 2.44.

Not verified: any other C library, and behavior under real memory
pressure.
