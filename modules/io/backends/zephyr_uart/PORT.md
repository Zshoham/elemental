# Port: io's writer on a Zephyr UART

This document follows the template in
[docs/process/porting.md](../../../../docs/process/porting.md).

| | |
|---|---|
| Interface | `el_writer` ([io.h](../../include/elemental/io.h)) |
| Platform facility | Zephyr's UART API: `uart_poll_out()` |
| Target | `elemental::io_zephyr_uart` |
| Header | [`elemental/io_zephyr_uart.h`](include/elemental/io_zephyr_uart.h) |
| Stability | Experimental |
| Verification level | **Emulator run**: `mps2/an385` (Cortex-M3, a CMSDK UART) under QEMU 10.2.4, against scripted devices and, with the board's UART1 joined to UART2, against a real one. Also run against scripted devices on `native_sim`: evidence about Zephyr integration, not a target CPU. **Symbol check**: also built for `qemu_cortex_m0` (Cortex-M0, ARMv6-M, nRF51 UART), not run. No hardware. |

Receiving on a UART is not here: a receive interrupt fills a ring, whose
reader is the stream. That is
[`ring_zephyr_uart`](../../../ring/backends/zephyr_uart/PORT.md).

## Capabilities

| Interface behavior | Supported | Notes |
|---|---|---|
| Passes every byte of a write to the driver | Yes | `el_zephyr_uart_poll`. One `uart_poll_out()` per byte, in order; a write returns once the last call has. `*n` is always `len`. Lossless only where the driver's `poll_out` waits until the device accepts the byte, as drivers of a hardware transmitter do: `uart_poll_out()` reports nothing, so a driver that discards does so unseen. Zephyr's RTT UART (`drivers/serial/uart_rtt.c`, v4.5.0) is one: its buffers are `SEGGER_RTT_MODE_NO_BLOCK_SKIP` and its `poll_out` ignores what `SEGGER_RTT_Write()` returns, so a byte its full buffer has no room for is skipped while the writer reports it written. |
| Would-block, no-space, end of stream, I/O error | No | The writer reports none of them. `uart_poll_out()` returns nothing, and the polled writer waits instead of stalling. |
| Failure detail | `device_is_ready()` | Only `init` can fail, with `EL_ERR_IO`: the device is not ready, or `dev` is null. |
| Sharing a device with a receiver | Yes | The polled writer may share a device with an `el_zephyr_uart_irq`. |

## Unsupported semantics

- **No interrupt-driven sender.** `el_zephyr_uart_poll` waits in the thread
  that writes, and must not be used from an interrupt handler. A sender that
  does not wait would be `el_zephyr_uart_irq_as_writer`, over a transmit ring
  (`docs/proposals/seams.md`, "Open questions"). Not built.
- **No asynchronous UART API** (`uart_callback_set`, `uart_tx`). The name
  `el_zephyr_uart_async` is kept for it.
- **The writer does not configure the device** (speed, parity, flow
  control) and does not close or power it down. The caller owns the device.

## Toolchain and runtime dependencies

A Zephyr application's build with `CONFIG_SERIAL`: `<zephyr/drivers/uart.h>`,
`<zephyr/device.h>`, the headers Zephyr's build generates, and the UART
driver. The library's one undefined symbol of its own is
`z_impl_device_is_ready`
([allowed-undefined.IO_ZEPHYR_UART.txt](../../../../tests/zephyr/allowed-undefined.IO_ZEPHYR_UART.txt)):
what `device_is_ready()` calls without `CONFIG_USERSPACE`. With
`CONFIG_USERSPACE` the same call is a system call, which was not built
here. The public header includes no Zephyr header.

Tested with Zephyr v4.5.0 and the Zephyr SDK 1.0.1's GCC 14.3.0.

## Configuration

`IO_ZEPHYR_UART`, on by default where it can build: it depends on `IO` and
`PLATFORM_ZEPHYR_UART`, a probed fact that is true in a Zephyr build whose
configuration has `CONFIG_SERIAL`. Interrupt-driven mode is not needed, and
neither is `ring`. The Zephyr test image gets it wherever Zephyr's
configuration has a serial subsystem: every `mps2/an385` image, the
`nucleo_g0b1re_renode` board's, and `native_sim`'s, whose image turns its
pseudo-terminal UARTs on (`tests/zephyr/boards/native_sim.conf`). Its tests
(`io_zephyr_uart.platform`) are in `io`'s image, so they run in the images
that hold `io`'s tests: `elemental.tests.io` on `mps2/an385` and
`native_sim`, and the configuration scenarios that hold every test
(`elemental.tests.variant`, `.assert_off`, `.crc_zephyr`) on `mps2/an385`.
The ARMv6-M build-only scenario (`elemental.symbols.armv6m`) builds the
library.

The writer needs neither interrupt-driven UARTs nor `ring`: shown on
2026-11-23 by a scratch Zephyr application for `mps2/an385` with
`CONFIG_SERIAL=y` and `CONFIG_UART_INTERRUPT_DRIVEN=n` in its `prj.conf`,
adding Elemental with the test image's defconfig plus
`# EL_CONFIG_RING is not set`. Elemental's configuration had
`PLATFORM_ZEPHYR_UART` and `IO_ZEPHYR_UART` and no `RING`; the application's
CMake failed the configure if `elemental::ring` or
`elemental::ring_zephyr_uart` existed or `elemental::io_zephyr_uart` did
not, and it linked a program that writes through `el_zephyr_uart_poll`,
with Elemental's warnings as errors. No test image runs in that
configuration: the writer's tests run where interrupt-driven UARTs are on.

Zephyr's `zephyr_interface` is linked for the build only
(`$<BUILD_INTERFACE:...>`), so that the target can be in Elemental's
installed export: shown by configuring, building and installing a Zephyr
application for `mps2/an385` that adds Elemental with `ELEMENTAL_INSTALL=ON`
and `IO_ZEPHYR_UART` and `RING_ZEPHYR_UART` on, and building a second one
against the installed package with
`find_package(Elemental COMPONENTS io_zephyr_uart ring_zephyr_uart)`.

## Concurrency and context

A sender may be used from one thread at a time, and may be moved while no
writer made from it is in use. It blocks as `uart_poll_out()` does (in most
drivers a busy loop), and is not for interrupt handlers.

## Verification

| Test | Kind | What it checks |
|---|---|---|
| `io_zephyr_uart.platform.*` | Backend-specific, against devices made by hand (`tests/fake_poll_uart.c`) | `init` reports a device that is not ready, and a null one; two writes pass every byte to the driver's `poll_out`, in order, and each reports all of its bytes written. |
| `ring_zephyr_uart.contract.*` | Shared contract suite | Both halves over the emulator's wire: the writer is UART1's polled sender, whose bytes are taken back from UART2 through a ring the receiver fills. Run with the receiver's tests: see [its PORT.md](../../../ring/backends/zephyr_uart/PORT.md#verification), which lists the skips. |
| `ring_zephyr_uart.wire.ThePolledWriterTakesEveryByteItIsGiven` | Backend-specific, over the emulator's wire | A write of 100 bytes reports them all written, and all of them arrive in order on the other end. |

**Run by Twister** on `mps2/an385` under QEMU 10.2.4: `io_zephyr_uart.platform`
in the `io` image; the wire tests in the `elemental.tests.ring.uart` image.
Each `io_zephyr_uart.platform` test was seen red: a writer whose `init`
reported every device ready failed the first, and one that dropped the last
byte of each write failed the second; both breaks were reverted.

**Not verified:**

- Real hardware, and whether a real driver's `poll_out` keeps every byte.
- A Cortex-M0+ run. `qemu_cortex_m0` builds the library and passes the
  symbol check; no Renode image was built or run here.
- `CONFIG_USERSPACE`.
- Timing: how long a write of many bytes holds the thread.
