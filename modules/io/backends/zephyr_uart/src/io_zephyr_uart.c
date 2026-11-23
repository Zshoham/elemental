#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/io_zephyr_uart.h>

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

static el_status poll_write(void *ctx, const void *data, size_t len, size_t *n)
{
    const el_zephyr_uart_poll *const uart = ctx;
    const uint8_t *const bytes = data;

    for (size_t i = 0; i < len; i++) {
        uart_poll_out(uart->dev, bytes[i]);
    }
    *n = len;
    return EL_OK;
}

static const el_writer_ops poll_writer_ops = {
    .write = poll_write,
};

el_status el_zephyr_uart_poll_init(el_zephyr_uart_poll *uart, const struct device *dev)
{
    EL_ASSERT(uart != NULL);

    uart->dev = dev;
    return device_is_ready(dev) ? EL_OK : EL_ERR_IO;
}

el_writer el_zephyr_uart_poll_as_writer(el_zephyr_uart_poll *uart)
{
    EL_ASSERT(uart != NULL);

    const el_writer writer = {.ctx = uart, .ops = &poll_writer_ops};
    return writer;
}
