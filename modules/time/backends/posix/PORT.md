# Port: time on POSIX

This document follows the template in
[docs/process/porting.md](../../../../docs/process/porting.md).

| | |
|---|---|
| Interface | `el_clock` ([time.h](../../include/elemental/time.h)) |
| Platform facility | `clock_gettime(CLOCK_MONOTONIC)` |
| Target | `elemental::time_posix` |
| Header | [`elemental/time_posix.h`](include/elemental/time_posix.h) |
| Stability | Experimental |
| Verification level | **Tested on a host**: Linux x86-64, glibc only |

## Capabilities

| Interface behavior | Supported | Notes |
|---|---|---|
| A reading that never goes back | Yes | The system's guarantee for `CLOCK_MONOTONIC`. |
| A constant frequency | Yes | 1 000 000 000: a tick is a nanosecond, whatever the resolution of the clock behind it. |
| Reading without blocking | Yes | One `clock_gettime` call. |
| Reading from several threads | Yes | The handle has no state. |

## Unsupported semantics

- **Not wall time.** The origin is unspecified, on Linux the boot. A
  reading cannot be turned into a date.
- **Does not count suspend on Linux.** `CLOCK_MONOTONIC` stops while the
  system is suspended; `CLOCK_BOOTTIME`, which does not, is not POSIX.
- **Its rate follows the system's.** It is not affected by jumps of the
  system time, but is by frequency adjustments (`clock_gettime(2)`), so a
  tick is a nanosecond as nearly as the system keeps time.
- No other clock: not `CLOCK_REALTIME`, not process or thread CPU time.

## Toolchain and runtime dependencies

`clock_gettime` and `CLOCK_MONOTONIC` from `<time.h>` (POSIX.1-2001; the
source defines `_POSIX_C_SOURCE 200809L`). No other symbol.

## Configuration

`TIME_POSIX`, on by default where CMake finds `<unistd.h>`
(`PLATFORM_POSIX`). The Zephyr test image turns it off
([tests/zephyr/elemental_defconfig](../../../../tests/zephyr/elemental_defconfig)):
its clock is [`time_zephyr`](../zephyr/PORT.md).

## Concurrency and context

The handle has no object and no state: it is valid for the life of the
program and may be made and read from any thread at once. `clock_gettime`
is async-signal-safe, so a signal handler may read it. It does not block.

## Verification

| Test | Kind | What it checks |
|---|---|---|
| `time_posix.contract.*` | Shared contract suite | Interface semantics. Time is made to pass by sleeping. |
| `time_posix.platform.*` | Backend-specific | A tick is a nanosecond; each reading lies between two readings of `CLOCK_MONOTONIC` taken around it; the handle is stateless. |

Skipped contract tests: none.

Run with GCC 16.2.1 and Clang 23.1.5, and under AddressSanitizer and
UndefinedBehaviorSanitizer, on Linux x86-64 with glibc 2.44.

Not verified: any other C library or operating system; behavior across a
suspend; a 32-bit `time_t`.
