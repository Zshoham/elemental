/*
 * uart_reader - an example: a UART's receive side as an el_reader, on
 * Zephyr.
 *
 * This is what goes between a device's receive interrupt and code that
 * reads a stream. The interrupt handler is the producer of an el_ring
 * (elemental/ring.h) and a thread is its consumer; the thread reads through
 * an el_reader, so the code behind the handle does not know there is a
 * UART. It is also how code logs from an interrupt handler: the handler is
 * given a logger, and the application gives that logger a deferred sink
 * (elemental/log_deferred.h).
 *
 *   reader side: copies out what the handler has received. Nothing yet ->
 *                EL_ERR_WOULD_BLOCK; uart_reader_wait() waits for more. It
 *                never ends.
 *
 * A full ring drops the bytes that do not fit: the handler cannot wait.
 * It counts them and logs a warning.
 *
 * Storage: the caller provides the uart_reader and the ring's storage and
 * keeps both alive, and in place, from uart_reader_start() to
 * uart_reader_stop().
 *
 * Concurrency: one thread reads and waits. The handler is the other
 * context; nothing here masks an interrupt except to read the counts.
 *
 * Needs a UART driver with CONFIG_UART_INTERRUPT_DRIVEN. Not part of
 * Elemental's API: an example, named without the el_ prefix, and not a
 * shipped backend.
 */
#ifndef UART_READER_H
#define UART_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <elemental/io.h>
#include <elemental/log.h>
#include <elemental/ring.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the handler has seen. */
typedef struct uart_reader_counts {
    uint32_t interrupts; /* times the handler ran */
    uint32_t bytes;      /* bytes taken from the device */
    uint32_t dropped;    /* of those, bytes the ring had no room for */
} uart_reader_counts;

typedef struct uart_reader {
    const struct device *dev;
    el_ring ring;              /* the handler writes, a thread reads */
    struct k_sem ready;        /* given by the handler when bytes arrive */
    const el_logger *log;      /* the handler's */
    uart_reader_counts counts; /* the handler's */
} uart_reader;

/*
 * Receive from `dev` into the `cap` bytes at `storage`, from now on: bytes
 * the device held before the call are discarded. `log` is the logger the
 * handler logs through. Its sink must not wait and its clock must be
 * readable from an interrupt handler: a deferred sink and Zephyr's clock.
 */
void uart_reader_start(uart_reader *rx, const struct device *dev, void *storage, size_t cap,
                       const el_logger *log);

/* Stop receiving. Bytes already received can still be read. */
void uart_reader_stop(uart_reader *rx);

el_reader uart_reader_as_reader(uart_reader *rx);

/*
 * Wait until the handler has received bytes since the last wait, or for
 * `timeout`. Returns false if the time ran out.
 */
bool uart_reader_wait(uart_reader *rx, k_timeout_t timeout);

/* A copy of the handler's counts, taken with interrupts masked. */
void uart_reader_counts_get(uart_reader *rx, uart_reader_counts *counts);

#ifdef __cplusplus
}
#endif

#endif /* UART_READER_H */
