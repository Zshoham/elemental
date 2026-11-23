#include "wire.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

const struct device *zephyr_wire_sender(void)
{
    return DEVICE_DT_GET(DT_NODELABEL(uart1));
}

const struct device *zephyr_wire_receiver(void)
{
    return DEVICE_DT_GET(DT_NODELABEL(uart2));
}

int zephyr_wire_poll_receive(unsigned char *byte, int timeout_ms)
{
    for (int waited = 0; waited <= timeout_ms; waited += 5) {
        if (uart_poll_in(zephyr_wire_receiver(), byte) == 0) {
            return 1;
        }
        k_sleep(K_MSEC(5));
    }
    return 0;
}

void zephyr_wire_flush(void)
{
    const struct device *const receiver = zephyr_wire_receiver();
    unsigned char byte;
    int quiet = 0;
    while (quiet < 5) {
        if (uart_poll_in(receiver, &byte) == 0) {
            quiet = 0;
        } else {
            quiet++;
            k_sleep(K_MSEC(10));
        }
    }
}
