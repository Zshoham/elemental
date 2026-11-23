# Port: time on Zephyr

This document follows the template in
[docs/process/porting.md](../../../../docs/process/porting.md).

| | |
|---|---|
| Interface | `el_clock` ([time.h](../../include/elemental/time.h)) |
| Platform facility | `k_uptime_ticks()` |
| Target | `elemental::time_zephyr` |
| Header | [`elemental/time_zephyr.h`](include/elemental/time_zephyr.h) |
| Stability | Experimental |
| Verification level | **Emulator run**: `mps2/an385` (Cortex-M3) under QEMU, and `nucleo_g0b1re_renode` (Cortex-M0+) under Renode, a modified model. Also run on `native_sim`, which is evidence about Zephyr integration, not about a target CPU. No hardware. |

## Capabilities

| Interface behavior | Supported | Notes |
|---|---|---|
| A reading that never goes back | Yes | The kernel's uptime, 64 bits. |
| A constant frequency | Yes | `CONFIG_SYS_CLOCK_TICKS_PER_SEC`, fixed when the application is built: 100 by default, 10 000 with a tickless kernel. |
| Reading without blocking | Yes | One kernel call and no arithmetic. |
| Reading from an interrupt handler | Yes, by an emulator run | Zephyr's header does not mark `k_uptime_ticks()` either way. Its implementation reads the tick count under a spin lock (`kernel/timeout.c`, v4.5.0), which an interrupt handler may take. Read from a `k_timer` expiry function, which Zephyr runs in interrupt context, on `mps2/an385` under QEMU ([examples/uart_ring](../../../../examples/uart_ring)). |

## Unsupported semantics

- **Not wall time.** The origin is when the kernel started.
- **No finer than a kernel tick.** At Zephyr's default of 100 ticks per
  second a reading moves in steps of 10 ms. The hardware cycle counter is
  finer and is not used: a tick is what the kernel's own timeouts count in.
- **Whether it counts time in a low-power state** is the board's timer
  driver's. Not examined.

## Toolchain and runtime dependencies

A Zephyr application's build: `<zephyr/kernel.h>`, the headers Zephyr's
build generates, and the kernel. The library's one undefined symbol is
`z_impl_k_uptime_ticks`
([allowed-undefined.TIME_ZEPHYR.txt](../../../../tests/zephyr/allowed-undefined.TIME_ZEPHYR.txt));
with `CONFIG_USERSPACE` the same inline call goes through a system call
instead, which was not built here. Tested with Zephyr v4.5.0 and the Zephyr
SDK 1.0.1's GCC 14.3.0.

## Configuration

`TIME_ZEPHYR`, on by default where CMake finds itself in a Zephyr build
(`PLATFORM_ZEPHYR`: Zephyr's `zephyr_library()` command exists). It cannot
be selected anywhere else. The application links Zephyr's interface to
Elemental's libraries, as
[tests/zephyr/CMakeLists.txt](../../../../tests/zephyr/CMakeLists.txt)
does.

## Concurrency and context

The handle has no object and no state: it is valid for the life of the
program and may be read from any thread. It does not block. It may be
read from an interrupt handler; see Capabilities for what that rests on.

## Verification

| Test | Kind | What it checks |
|---|---|---|
| `time_zephyr.contract.*` | Shared contract suite | Interface semantics. Time is made to pass by `k_sleep()`. |
| `time_zephyr.platform.*` | Backend-specific | A tick is a kernel tick; each reading lies between two readings of `k_uptime_ticks()` taken around it; a tenth of a second of sleep is about a tenth of a second of ticks; the handle is stateless. |

Skipped contract tests: none.

Run by Twister in the `elemental.tests.time` image on `native_sim` and
`mps2/an385` under QEMU, both at 100 ticks per second, and on
`nucleo_g0b1re_renode` under Renode 1.17.0 at 10 000. An
emulator's timer is the emulator's: these runs show the port reads the
kernel's uptime in the right unit, not that a real board's timer keeps
time.

Reading from an interrupt handler is checked by the UART example's test,
`example.uart_ring`, in the `elemental.tests.ring.uart` image on
`mps2/an385` under QEMU: a logger whose clock is this one, wrapped to note
where it is read from, logs from a `k_timer` expiry function (interrupt
context), and each record's
time lies between two readings taken by the thread around it. One board,
one emulator, three readings.

Not verified: real hardware; `CONFIG_USERSPACE`; reading from an interrupt
handler on ARMv6-M or while a thread is inside `k_uptime_ticks()` itself;
a kernel tick rate other than the two the boards were built with.
