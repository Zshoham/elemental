/*
 * Contract fixture for a UART's two ends on mps2/an385 under QEMU, where
 * uart1's output is uart2's input (tests/zephyr/CMakeLists.txt). Both
 * halves: the reader is the reader of a ring that elemental::ring_zephyr_uart
 * fills from uart2, which has no end while it runs; the writer is uart1's
 * polled sender (elemental::io_zephyr_uart), and what it sent is taken back
 * from uart2.
 */
#include "fixture_uart.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <elemental/io_contract.h>
#include <elemental/io_zephyr_uart.h>
#include <elemental/ring.h>
#include <elemental/ring_zephyr_uart.h>

#include "wire.h"

static el_zephyr_uart_poll sender;
static el_ring rx;
static el_zephyr_uart_irq receiver;
static K_SEM_DEFINE(arrived, 0, 1);
/* Twice the most the suite sends before it takes: a byte the ring drops
 * would fail a correct sender. */
static uint8_t received[2 * EL_IO_CONTRACT_SIZE_MAX];
static int receiving;
static size_t sent;  /* accepted by the sender since the stream was made */
static size_t taken; /* handed to the suite by writer_take */

/* Runs in uart2's receive interrupt. */
static void on_arrival(void *ctx)
{
    k_sem_give(ctx);
}

static void reset(void *ctx)
{
    (void)ctx;
    if (receiving) {
        el_zephyr_uart_irq_deinit(&receiver);
        receiving = 0;
    }
}

static el_reader receive(void)
{
    k_sem_reset(&arrived); /* nothing left over from the previous stream */
    el_ring_init(&rx, received, sizeof(received));
    if (el_zephyr_uart_irq_init(&receiver, zephyr_wire_receiver(), &rx, on_arrival, &arrived) !=
            EL_OK ||
        el_zephyr_uart_poll_init(&sender, zephyr_wire_sender()) != EL_OK) {
        abort(); /* the wire is not there: nothing could be verified */
    }
    receiving = 1;
    sent = 0;
    taken = 0;
    return el_ring_as_reader(&rx);
}

/* Until `len` bytes wait in the receiver. The wire loses nothing. */
static void await(size_t len)
{
    for (int waits = 0; el_ring_len(&rx) < len; waits++) {
        if (waits == 400) {
            abort(); /* four seconds: the wire or the handler is broken, nothing could be verified
                      */
        }
        (void)k_sem_take(&arrived, K_MSEC(10));
    }
}

static el_reader reader_with_content(void *ctx, const uint8_t *content, size_t len)
{
    (void)ctx;
    const el_reader reader = receive();
    if (el_write_all(el_zephyr_uart_poll_as_writer(&sender), content, len, NULL) != EL_OK) {
        abort(); /* the wire is broken: nothing could be verified */
    }
    await(len);
    return reader;
}

static el_reader reader_stalled(void *ctx)
{
    (void)ctx;
    return receive(); /* nothing sent: nothing to give, and not ended */
}

/* The sender, counted, so that writer_take knows what to wait for. */
static el_status counted_write(void *ctx, const void *data, size_t len, size_t *n)
{
    (void)ctx;
    const el_status status = el_write(el_zephyr_uart_poll_as_writer(&sender), data, len, n);
    sent += *n;
    return status;
}

static const el_writer_ops counted_ops = {.write = counted_write};

static el_writer writer_with_room(void *ctx, size_t room)
{
    (void)ctx;
    (void)room; /* a polled sender takes every byte it is given */
    (void)receive();
    return (el_writer){.ctx = NULL, .ops = &counted_ops};
}

static size_t writer_take(void *ctx, uint8_t *out, size_t cap)
{
    (void)ctx;
    await(sent - taken);
    size_t n = 0;
    el_status_ignore(el_read(el_ring_as_reader(&rx), out, cap, &n));
    taken += n;
    return n;
}

const el_io_contract_fixture zephyr_uart_fixture = {
    .name = "io_zephyr_uart on uart1 to ring_zephyr_uart on uart2",
    .reset = reset,
    .reader_with_content = reader_with_content,
    .reader_then = EL_IO_CONTRACT_THEN_STALLS,
    .reader_stalled = reader_stalled,
    .writer_with_room = writer_with_room,
    .writer_take = writer_take,
};
