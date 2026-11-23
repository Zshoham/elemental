# Port: a ring fed by a Zephyr UART's receive interrupt

This document follows the template in
[docs/process/porting.md](../../../../docs/process/porting.md). This backend
implements no interface: it is the producer of a ring
([ring.h](../../include/elemental/ring.h)) that the caller owns, fed from
one facility. What the application reads through is the ring's reader, an
`el_reader` ([io.h](../../../io/include/elemental/io.h)).

| | |
|---|---|
| Object | `el_ring`'s producer side, and the ring's `el_reader` for the thread |
| Platform facility | Zephyr's UART API, interrupt-driven: `uart_irq_*()` and `uart_fifo_read()` in the handler; `uart_poll_in()` to discard what the device held before `init` |
| Target | `elemental::ring_zephyr_uart` |
| Header | [`elemental/ring_zephyr_uart.h`](include/elemental/ring_zephyr_uart.h) |
| Stability | Experimental |
| Verification level | **Emulator run**: `mps2/an385` (Cortex-M3, a CMSDK UART) under QEMU 10.2.4, with the board's UART1 joined to UART2. Also run against scripted devices on `native_sim`: evidence about Zephyr integration, not a target CPU. **Symbol check**: also built for `qemu_cortex_m0` (Cortex-M0, ARMv6-M, nRF51 UART), not run. No hardware. |

Sending on a UART is
[`io_zephyr_uart`](../../../io/backends/zephyr_uart/PORT.md), the polled
writer.

## Capabilities

| Behavior | Supported | Notes |
|---|---|---|
| The ring is the caller's | Yes | The caller sets it up with `el_ring_init()`, empty and open, and passes it to `el_zephyr_uart_irq_init()`. From an `init` that returns `EL_OK` until `deinit` returns, its producer side (`el_ring_write`, `el_ring_room`, `el_ring_close`, its writer) is the receive handler's; the consumer side (`el_ring_read`, `el_ring_len`, its reader) is the caller's, from one context. |
| Reader: partial reads, oldest first | Yes | The ring's reader: `1 <= *n <= cap`. |
| Reader: stalls without ending | Yes | `EL_ERR_WOULD_BLOCK` while running and empty. A running receiver has no end. |
| Reader: end of stream | Yes, after `el_zephyr_uart_irq_deinit()`, once the bytes received before it are read | `deinit` closes the ring, from the calling thread, after the callback is cleared. Permanent. The ring and its storage stay in use while the reader drains them: keep both alive and in place until the reader is no longer used, not only until `deinit`. |
| A failed `init` | Leaves the ring as it was | Open and not written; the handler is not installed, and the producer side is still the caller's. The caller acts on the status: it may close the ring, so that its reader ends, or keep it. |
| Waiting for bytes | The application's | `on_arrival(ctx)` is called by the receive handler after bytes arrived: those that fit are in the ring, the rest counted as dropped, so with capacity 0 or a full ring it is called with nothing to read. The handler normally runs in one of the device's interrupts, but may run synchronously inside `init` (see "Concurrency and context"). The application gives a semaphore, event, signal or work item. No primitive is chosen here, and no layout depends on Zephyr's configuration. |
| Overrun | Counted | Bytes the ring has no room for are dropped, and `el_zephyr_uart_irq_dropped()` reports them, modulo 2^32. The handler cannot wait. |
| Failure detail | `device_is_ready()` | Only `init` can fail, with `EL_ERR_IO`, and only for what the driver reports: a device that is not ready, an asynchronous receive active on it (`uart_poll_in()` gives `-EBUSY`), or a callback setter that reports an error (a driver with no interrupt-driven API). The caller tells the first apart with `device_is_ready()`; there is no errno accessor. A null ring is a contract violation, asserted. |
| A sender on the same device | Yes | The polled writer (`io_zephyr_uart`) may share a device with a receiver. |

## Unsupported semantics

- **No interrupt-driven sender.** A sender that does not wait would be
  `el_zephyr_uart_irq_as_writer`, over a transmit ring
  (`docs/proposals/seams.md`, "Open questions"). Not built.
- **No asynchronous UART API** (`uart_callback_set`, `uart_rx_enable`). The
  name `el_zephyr_uart_async` is kept for it.
- **The receiver does not configure the device** (speed, parity, flow
  control) and does not close or power it down. The caller owns the device.
- **The receiver owns the device's interrupt callback and receive
  interrupt** from `init` to `deinit`. Another receiver, or other code that
  sets the callback, on the same device is a conflict this backend does not
  detect.
- **Nothing checks that the ring is empty and open at `init`.** The ring
  has no predicate for it, and `el_ring_room()` alone cannot tell a closed
  ring from one of capacity 0 (a nonzero read through its reader could,
  as end of stream against would-block, but `init` does not read the
  caller's ring). It stays the caller's precondition. A closed ring takes
  nothing: every byte would be counted as dropped. Bytes already in it
  would be read before what is received.
- **One CPU.** `deinit` is safe on one CPU. On several, another CPU may still
  be inside the handler when it returns, so multi-core targets are not
  supported until that is shown.
- **Only drivers that call the callback from the device's interrupt.** On
  one CPU the reading thread cannot preempt that interrupt, which is what
  makes `deinit` safe and lets `on_arrival` call `el_ring_len()`. Drivers
  that call it from a work queue or a thread are not supported: in Zephyr
  v4.5.0 the Bluetooth NUS UART (`drivers/serial/uart_bt.c`,
  `cb_work_handler`) and USB CDC ACM
  (`subsys/usb/device_next/class/usbd_cdc_acm.c`, `cdc_acm_irq_cb_handler`;
  the legacy stack's `subsys/usb/device/class/cdc_acm.c` too) call it from
  a work item. There a callback that gives a semaphore can be preempted by
  the thread it wakes; that thread can call `deinit`, which disables the
  interrupt flag and clears the callback pointer and returns while the
  callback is still running, to resume after the receiver was released.
  Clearing the callback does not wait for a call in progress, and Zephyr has
  no call that tells the two kinds of driver apart, so no code here can.
- **The caller selects an instance with interrupt-driven receive.**
  `CONFIG_UART_INTERRUPT_DRIVEN` is build-wide, and Zephyr's UART API has no
  call that reports whether one instance has it. `EL_ERR_IO` covers only
  what the driver reports. Zephyr's nRF UARTE
  (`drivers/serial/uart_nrfx_uarte.c`, v4.5.0) puts the callback setter in
  the table shared by every instance when any instance has
  `CONFIG_UART_<n>_INTERRUPT_DRIVEN`; on an instance without it, its
  `int_driven` pointer is null and the setter writes through it, so `init`
  on that instance is not an `EL_ERR_IO` but the driver's fault.
- **Bytes the device held before `init` are discarded only where the driver
  can say what it holds.** The discard reads with `uart_poll_in()` until it
  gives anything but a byte. `-1` is empty, as is any other answer but the
  two below: some drivers report empty with `-ENODATA` or `-EPERM`. With an
  asynchronous receive active (`-EBUSY`) `init` fails with `EL_ERR_IO`,
  leaving the ring as it was. A driver without `poll_in` (`-ENOSYS`) cannot
  be drained, and what it held before `init` may be the first bytes
  received. Draining through `uart_fifo_read()` instead is not done: Zephyr
  leaves its result undefined outside the interrupt handler. Bytes read by
  the discard are gone even if `init` then fails.
- **Line errors are not reported.** Parity, framing and overrun errors in the
  device are the driver's; this backend does not enable the error interrupt.
- **Wake-up is "bytes arrived", not how many.** A thread it wakes reads until
  `EL_ERR_WOULD_BLOCK`.

## Toolchain and runtime dependencies

A Zephyr application's build with `CONFIG_SERIAL` and
`CONFIG_UART_INTERRUPT_DRIVEN`: `<zephyr/drivers/uart.h>`, `<zephyr/device.h>`,
the headers Zephyr's build generates, and the UART driver. The library's one
undefined symbol of its own is `z_impl_device_is_ready`
([allowed-undefined.RING_ZEPHYR_UART.txt](../../../../tests/zephyr/allowed-undefined.RING_ZEPHYR_UART.txt)):
what `device_is_ready()` calls without `CONFIG_USERSPACE`. `el_ring_write`
and `el_ring_close` come from `ring`, and `memcpy` from `ring` and `io`, as
for every build. With `CONFIG_USERSPACE` the same call is a system call,
which was not built here. No compiler helper and no `__atomic_*` call: the
load and store of one word are instructions on both boards (ADR 0023). The
public header includes no Zephyr header.

Tested with Zephyr v4.5.0 and the Zephyr SDK 1.0.1's GCC 14.3.0.

## Configuration

`RING_ZEPHYR_UART`, on by default where it can build: it depends on `RING`
and `PLATFORM_ZEPHYR_UART_IRQ`, a probed fact that is true in a Zephyr build
whose configuration has `CONFIG_SERIAL` and `CONFIG_UART_INTERRUPT_DRIVEN`.
An application that wants the backend sets the second in its `prj.conf`.
Both are build-wide options, not properties of a device. The Zephyr test
image gets it wherever Zephyr's configuration has both: Zephyr's `mps2`
board defaults `CONFIG_UART_INTERRUPT_DRIVEN` on when `CONFIG_SERIAL` is,
and the `nucleo_g0b1re_renode` board's defconfig sets it. So every
`mps2/an385` image builds the backend, and the Renode board's configuration
selects it. `native_sim`'s image turns its pseudo-terminal UARTs
interrupt-driven (`tests/zephyr/boards/native_sim.conf`), so it builds the
backend too. The tests are in `ring`'s image, so
`ring_zephyr_uart.platform` runs only in the images that hold `ring`'s
tests: `elemental.tests.ring` on `mps2/an385` and `native_sim`,
`elemental.tests.ring.uart`, and the configuration scenarios that hold
every test (`elemental.tests.variant`, `.assert_off`, `.crc_zephyr`) on
`mps2/an385`. The tests that need the wire (`ring_zephyr_uart.contract`,
`ring_zephyr_uart.wire`) run only in `elemental.tests.ring.uart`, on
`mps2/an385` under QEMU; they also need `IO_ZEPHYR_UART`, whose polled
writer sends. The ARMv6-M build-only scenario (`elemental.symbols.armv6m`)
sets the option itself.

`elemental::ring_zephyr_uart` depends on `ring` (and through it on `io`).
Zephyr's `zephyr_interface` is linked for the build only
(`$<BUILD_INTERFACE:...>`), so that the target can be in Elemental's
installed export: shown by configuring, building and installing a Zephyr
application for `mps2/an385` that adds Elemental with `ELEMENTAL_INSTALL=ON`
and `IO_ZEPHYR_UART` and `RING_ZEPHYR_UART` on, and building a second one
against the installed package with
`find_package(Elemental COMPONENTS io_zephyr_uart ring_zephyr_uart)`.

## Concurrency and context

The receive handler is the ring's producer and calls `on_arrival`. One
thread reads, and calls `init` and `deinit`; `el_ring_len()` is that
thread's (it is the ring's consumer-side count, so also safe in
`on_arrival`, which on one CPU cannot overlap the reader), and
`el_zephyr_uart_irq_dropped()` may be called from any context. The receiver
and the ring must not move from `init` until `deinit` returns: the handler
holds both. The ring and its storage must not move or end until the reader
is no longer used, which may be after `deinit`; the device and the
callback's `ctx` are used from `init` until `deinit` returns. Nothing here
blocks.

`on_arrival` usually runs in interrupt context, but not always in the
receive interrupt, nor always in an interrupt. Some drivers call the
callback from the device's other interrupts as well: Zephyr's STM32 driver
(`drivers/serial/uart_stm32.c`, v4.5.0), with `CONFIG_PM`, enables the
transmission-complete interrupt from `poll_out`, calls the callback from
it, and reports "RX ready" from the hardware flag whether or not the
receive interrupt is enabled. So the callback can run as soon as `init`
sets it, before `init` enables the receive interrupt; the caller's ring is
therefore open before `init` sets the callback, and `init` does not touch
it. And Zephyr's CMSDK APB driver, the one `mps2/an385` uses
(`drivers/serial/uart_cmsdk_apb.c`, v4.5.0), "primes" the interrupt by
calling its handler as an ordinary function, under `irq_lock()`, from
`uart_irq_rx_enable()` when a byte is already waiting. A byte that arrives
after `init`'s discard and before it enables the interrupt therefore runs
`on_arrival` on the thread calling `init`, before `init` returns. So the
callback and its `ctx` must be ready before `init` is called, it must not
block, and it must not assume `k_is_in_isr()`: `k_sem_give()`,
`k_event_post()`, `k_poll_signal_raise()` and `k_work_submit()` may be
called from it. The handler calls no Elemental logging: an application that
logs from an interrupt handler does so through the deferred sink
([examples/uart_ring](../../../../examples/uart_ring)).

## Verification

| Test | Kind | What it checks |
|---|---|---|
| `ring_zephyr_uart.contract.*` | io's shared contract suite | Both ends of the wire: the reader is the reader of the ring the receiver fills from UART2, and stalls after its content; the writer is UART1's polled sender (`io_zephyr_uart`), whose bytes are taken back from UART2 through the ring (`tests/fixture_uart.c`). 13 tests: 9 run, 4 skipped. |
| `ring_zephyr_uart.wire.*` | Backend-specific, in a real receive interrupt over the emulator's wire | The polled writer takes every byte it is given and all of it arrives in order; a byte the device held before init is discarded; a full ring drops the excess, counts it, keeps the first bytes in order and takes bytes again once read; `on_arrival` is called in interrupt context, after the bytes are in the ring, and, in these tests' rings, which have room when bytes arrive, never on an empty ring (a call does not promise readable bytes: with capacity 0 every byte is dropped); after `deinit` returns, a byte sent is still in the device, polled back from it with a bounded wait, and `on_arrival` was not called nor the ring changed; `deinit` ends the reader after the bytes received, for good. 6 tests. |
| `ring_zephyr_uart.platform.*` | Backend-specific, against devices made by hand (`tests/fake_devices.c`) | A failed init leaves the caller's ring open, empty and its producer side the caller's, for no device, a device that is not ready, and a driver with no interrupt-driven receive; an asynchronous receive on the device (`uart_poll_in()` gives `-EBUSY`) fails init before the callback is set, leaving the ring open; a driver without `poll_in` (`-ENOSYS`) has its held bytes received, not discarded; bytes that arrive as the callback is set, taken by the callback run then from another of the device's interrupts, before the receive interrupt is enabled, are kept and readable, none dropped; init turns the receive interrupt off, discards held bytes, sets the callback, then enables the interrupt, and deinit disables before it clears; deinit closes the ring after the bytes received, which are still read, and the ring then takes nothing; every run of the handler calls `uart_irq_update()` once, before `uart_irq_rx_ready()`, whose status the scripted device latches in the update (checked after each test); a driver whose "RX ready" is edge-triggered is read until its FIFO gives less than asked; the handler keeps 40 bytes (more than it reads at once) in order and calls `on_arrival` once per interrupt that took bytes, and not for one that took none; a full ring is counted before `on_arrival` is called, across interrupts; the count wraps modulo 2^32 (seeded through a C helper's relaxed store, not through C++); an interrupt after `deinit` is neither taken nor reported. 11 tests. |
| `example.uart_ring.*` | Example's test, in the `elemental.tests.ring.uart` image | The example on the same wire: on an empty ring a read reports `EL_ERR_WOULD_BLOCK` with nothing read; one byte sent gives the semaphore `on_arrival` gives, within a bounded wait, and is then read back; two lines arrive whole and in order through the example's read loop; a `k_timer` expiry function logs from interrupt context through the deferred sink and the thread drains it, each record's clock read in the interrupt; the thread reports drops from `el_zephyr_uart_irq_dropped()`. |

Skipped contract tests, each by a capability the fixture leaves null and the
reason:

- `ReaderEndOfStreamIsPermanent`: the reader stalls after its content
  (`reader_then` is `EL_IO_CONTRACT_THEN_STALLS`): a running receiver has no
  end. It ends only after `deinit`, which `ring_zephyr_uart.wire` checks.
- `WriterEndOfStreamIsPermanentAndKeepsWhatFit`: no `writer_ending_after`: a
  UART cannot be made to end after an exact byte count, and the polled writer
  never reports an end.
- `WriterNoSpaceKeepsWhatFitAndRepeats`: no `writer_full_after`: the polled
  writer never reports `EL_ERR_NO_SPACE`; it waits.
- `StalledWriterReportsWouldBlock`: no `writer_stalled`: the polled writer
  never stalls; it waits.

**Run by Twister** in the `elemental.tests.ring.uart` image on `mps2/an385`
under QEMU 10.2.4 (a UART's wire is one socket joining QEMU's second and third
serial ports: `ELEMENTAL_ZEPHYR_UART_WIRE`, tests/zephyr/CMakeLists.txt). The
`ring_zephyr_uart.platform` tests need no wire; they run in the images that
hold `ring`'s tests and have `RING_ZEPHYR_UART` (see "Configuration"). Each
test was seen red when the receiver was written: a temporary break of the
backend (or, for the example, of the interrupt handler in `isr_tick.c`)
failed it, then was reverted; removing the handler's `uart_irq_update()`
failed seven scripted tests; treating every `uart_poll_in()` answer but `-1`
as a failure failed the `-ENOSYS` test; a `deinit` that left the receive
interrupt and callback in place but no longer called `on_arrival` failed the
wire test's poll of the device; and a handler that never called
`on_arrival` failed the example's bounded wait. After the ring became the
caller's (2026-11-23): an `init` that closed the ring on failure failed
`AFailedInitLeavesTheRingOpenAndUntouched` and the `-EBUSY` test, and a
`deinit` that did not close the ring failed
`DeinitClosesTheRingAfterWhatWasReceived` and the wire test
`DeinitEndsTheReaderAfterWhatWasReceived`; both breaks were reverted.

**Not verified:**

- Real hardware. The emulated CMSDK UART has a one-byte receive buffer (the
  driver's `UART_RX_BF`), and the tests never send more than one byte while
  no receiver is listening, so how the model treats a second one was not
  examined. A real UART whose FIFO overruns loses bytes the ring never sees,
  which this backend cannot count.
- A Cortex-M0+ run. `qemu_cortex_m0` (a Cortex-M0) builds the library and
  passes the symbol check. The `nucleo_g0b1re_renode` board's configuration
  selects the backend, so its `ring` image would run
  `ring_zephyr_uart.platform`, but Renode and `hal_stm32` are not installed
  here: no Renode image was built or run with it, and its RAM headroom with
  the extra tests is unchecked.
- An interrupt taken between `init`'s callback and its `uart_irq_rx_enable`,
  or `deinit`'s disable and its callback clear, on real hardware. The driver
  run here (`uart_cmsdk_apb`) calls the callback from `uart_irq_rx_enable()`
  if a byte is waiting; a callback run from another interrupt as soon as it
  is set is checked only against a scripted device.
- A driver that caches the receive status in `uart_irq_update()`
  (`uart_intel_lw`, say) on its device; the scripted device does so.
- More than one CPU.
- `CONFIG_USERSPACE`, where the device and its callback may belong to a user
  thread.
- Timing: that a handler keeps up with a baud rate on a real clock.
