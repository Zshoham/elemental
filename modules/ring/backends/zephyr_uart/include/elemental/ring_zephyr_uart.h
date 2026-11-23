/*
 * elemental/ring_zephyr_uart.h - a Zephyr UART's receive interrupt filling
 * a ring.
 *
 * STATUS: experimental. See docs/process/api-stability.md, and PORT.md next
 * to this backend for what it does and does not support.
 *
 *   el_zephyr_uart_irq   the producer of the caller's el_ring, fed by the
 *                        device's receive interrupt
 *
 * The receiver is not a reader. What it receives is in the caller's ring,
 * and the reader is the ring's: el_ring_as_reader(). Sending on a UART is
 * elemental/io_zephyr_uart.h.
 *
 * This header includes none of Zephyr's: `struct device` is declared and
 * used by pointer only, as time_zephyr.h uses none of Zephyr's types. The
 * application includes <zephyr/device.h> for DEVICE_DT_GET().
 *
 * Ownership: the caller owns the device, and keeps it alive and
 * initialized while the receiver is running. This backend never configures
 * it (speed, parity), and does not change what it does not use. A receiver
 * owns the device's interrupt callback and receive interrupt from init to
 * deinit; no other code may set the callback of a device it receives on.
 * The caller owns the ring and its storage; the receiver borrows the ring's
 * producer side (see "The ring").
 *
 * Blocking: nothing here waits, allocates, starts a thread, or takes a
 * lock.
 *
 * More than one CPU is not supported, nor is a driver that calls back from
 * a work queue or a thread: see "Contexts" and "Drivers" below.
 *
 * Configuration: RING_ZEPHYR_UART, which needs a Zephyr build with
 * CONFIG_SERIAL and CONFIG_UART_INTERRUPT_DRIVEN.
 *
 * No layout in this header depends on Zephyr's configuration: it has no
 * kernel object, such as a semaphore, in it. What a reading thread waits on
 * is the application's (see "Waiting").
 */
#ifndef ELEMENTAL_RING_ZEPHYR_UART_H
#define ELEMENTAL_RING_ZEPHYR_UART_H

#include <stdint.h>

#include <elemental/ring.h>
#include <elemental/status.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device; /* Zephyr's, <zephyr/device.h>: used by pointer only */

/*
 * Interrupt-driven. From init on, the device's receive interrupt copies
 * what arrives into the caller's ring; bytes the device held before init
 * are discarded where the driver can say what it holds (see "Discard").
 * What does not fit is dropped and counted: the handler cannot wait.
 *
 * The ring. The caller sets the ring up with el_ring_init(), empty and
 * open, before init. From an init that returns EL_OK until deinit returns,
 * the ring's producer side (el_ring_write, el_ring_room, el_ring_close and
 * the ring's writer) belongs to the receive handler: the caller must not
 * use it. The consumer side (el_ring_read, el_ring_len and the reader from
 * el_ring_as_reader) is the caller's, from one context. Read through it:
 *
 *   EL_OK                  1 <= *n <= cap bytes received, oldest first.
 *   EL_ERR_WOULD_BLOCK     Nothing has arrived. Not an end: a running
 *                          receiver has none.
 *   EL_ERR_END_OF_STREAM   After deinit, once every byte received before it
 *                          has been read.
 *
 * Waiting is the application's. `on_arrival`, if not null, is called as
 * on_arrival(ctx) by the receive handler, once per run of it that took
 * bytes from the device, after they are in the ring or, those that did
 * not fit, counted as dropped. So a call says that bytes arrived, not that
 * the ring holds any: with a ring of capacity 0, or a full one, every one
 * was dropped. Nor how many, nor that they are still there: a thread it
 * wakes reads until EL_ERR_WOULD_BLOCK. The handler normally runs in
 * interrupt context, in any of the device's interrupts (some drivers call
 * it for a transmission completing too, as soon as it is set), but a
 * driver may also run it synchronously, from inside
 * el_zephyr_uart_irq_init(), on the initializing thread with interrupts
 * locked: Zephyr's CMSDK APB driver (mps2) does so when a byte is waiting
 * as the receive interrupt is enabled. So `on_arrival` and its `ctx` must
 * be ready before init is called, and it must be fit for both: it must not
 * block, and must not assume k_is_in_isr(). k_sem_give(), k_event_post(),
 * k_poll_signal_raise() and k_work_submit() may be called from it. `ctx`
 * is borrowed from init until deinit returns.
 *
 * No lost wake-up: the handler puts bytes in the ring before it calls
 * on_arrival, so bytes stored after a thread's last read always have a call
 * after them. A semaphore with a limit of 1 therefore saturates harmlessly:
 * the thread takes it, then reads until it is told EL_ERR_WOULD_BLOCK.
 * A thread that waits with a timeout and reads on every wake, timed out or
 * not, also loses nothing.
 *
 * Contexts. The receive handler is the ring's producer and calls
 * on_arrival. One thread reads, and calls init and deinit. el_ring_len()
 * on the ring is the reading thread's, and also safe in on_arrival, which
 * on one CPU cannot overlap the reader; el_zephyr_uart_irq_dropped() may be
 * called from any context. Deinit is safe on one CPU; on several, another
 * CPU may still be inside the handler when it returns, so multi-core
 * targets are not supported until that is shown.
 *
 * Drivers. The receiver is supported only on a driver that calls the
 * callback from the device's interrupt, which on one CPU the reading thread
 * cannot preempt: what makes deinit safe, and el_ring_len() in on_arrival.
 * Drivers that call it from a work queue or a thread are not supported:
 * Zephyr's Bluetooth NUS UART (uart_bt) and USB CDC ACM do, from a work
 * item. There the callback can be preempted by the thread that calls
 * deinit, which then returns while the callback is still running, and
 * clearing the callback does not wait for it. Zephyr has no call that
 * tells the two kinds of driver apart.
 *
 * Discard. Init reads what the device holds with uart_poll_in() until it
 * reports anything but a byte. Bytes so read are gone, even if init then
 * fails. With an asynchronous receive active on the device (-EBUSY) init
 * fails. A driver without poll_in (-ENOSYS) cannot be drained: the bytes it
 * held before init may be the first received.
 *
 * Storage: the caller provides the el_zephyr_uart_irq, the el_ring and the
 * ring's storage. The device and the callback are used from init until
 * deinit returns. Neither the receiver nor the ring may move from init
 * until deinit returns: the handler holds both addresses. The ring and its
 * storage stay alive, and the ring in place, for as long as its reader is
 * used, which may be after deinit, while it gives the bytes received before
 * it. A ring holds its capacity of data, so that is the most the reading
 * thread can fall behind by.
 *
 * The fields are public so that the object can be allocated, and are not
 * API; use the functions.
 */
typedef struct el_zephyr_uart_irq {
    const struct device *dev;
    el_ring *ring;                 /* the caller's; its producer side is the handler's */
    void (*on_arrival)(void *ctx); /* the handler's */
    void *on_arrival_ctx;
    el_ring_word dropped; /* the handler's */
} el_zephyr_uart_irq;

/*
 * Start receiving from `dev` into `ring`. `uart` and `ring` must not be
 * null; `ring` is set up, empty and open (see "The ring"). `on_arrival`
 * may be null.
 *
 * The caller selects a device whose instance has interrupt-driven receive.
 * CONFIG_UART_INTERRUPT_DRIVEN is build-wide, and Zephyr has no call that
 * reports whether one instance has it. A driver may give every instance the
 * callback setter when any has it: Zephyr's nRF UARTE does, and on an
 * instance with CONFIG_UART_<n>_INTERRUPT_DRIVEN off its setter writes
 * through a null pointer. That is not reported, and is not EL_ERR_IO.
 *
 * EL_OK: receiving, and the ring's producer side is the handler's until
 * deinit returns. EL_ERR_IO, only for what the driver reports: the device
 * is not ready (a null `dev` is not); uart_poll_in() reports an
 * asynchronous receive (-EBUSY); or setting the callback reports an error
 * (a driver with no interrupt-driven API). device_is_ready() tells the
 * first apart. A failed init leaves the ring as it was, open and not
 * written, and the handler is not installed: nothing runs, the ring's
 * producer side is still the caller's, and the caller acts on the status
 * (closing the ring, say, so that its reader ends). Deinit is not called
 * after a failed init.
 */
EL_NODISCARD el_status el_zephyr_uart_irq_init(el_zephyr_uart_irq *uart, const struct device *dev,
                                               el_ring *ring, void (*on_arrival)(void *ctx),
                                               void *ctx);

/*
 * Stop receiving, after an init that returned EL_OK: the receive interrupt
 * is disabled, the callback cleared, so on_arrival is not called once this
 * returns, and then the ring is closed, from the calling thread, whose
 * producer side it is again. The bytes already received can still be
 * read, and then the ring's reader reports EL_ERR_END_OF_STREAM.
 */
void el_zephyr_uart_irq_deinit(el_zephyr_uart_irq *uart);

/*
 * Bytes dropped because the ring was full, since init, modulo 2^32: compare
 * two readings by subtraction. Any context.
 */
uint32_t el_zephyr_uart_irq_dropped(const el_zephyr_uart_irq *uart);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_RING_ZEPHYR_UART_H */
