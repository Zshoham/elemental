#include "uart_wire.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include <elemental/time_zephyr.h>

static const struct device *const sender = DEVICE_DT_GET(DT_NODELABEL(uart1));
static const struct device *const receiver = DEVICE_DT_GET(DT_NODELABEL(uart2));

const struct device *uart_wire_receiver(void)
{
    return device_is_ready(sender) && device_is_ready(receiver) ? receiver : NULL;
}

void uart_wire_send(const void *data, size_t len)
{
    const unsigned char *const bytes = data;
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(sender, bytes[i]);
    }
}

static el_ticks context_now(void *ctx)
{
    context_clock *const clock = ctx;
    if (k_is_in_isr()) {
        clock->from_interrupt++;
    } else {
        clock->from_thread++;
    }
    return el_clock_now(el_zephyr_clock());
}

static uint32_t context_ticks_per_second(void *ctx)
{
    (void)ctx;
    return el_clock_ticks_per_second(el_zephyr_clock());
}

static const el_clock_ops context_ops = {
    .now = context_now,
    .ticks_per_second = context_ticks_per_second,
};

void context_clock_init(context_clock *clock)
{
    clock->from_interrupt = 0;
    clock->from_thread = 0;
}

el_clock context_clock_as_clock(context_clock *clock)
{
    return (el_clock){.ctx = clock, .ops = &context_ops};
}
