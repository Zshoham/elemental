#include "fake_poll_uart.h"

#include <string.h>

#include <zephyr/drivers/uart.h>

#define SENT_MAX 64

static struct device_state not_ready_state = {.init_res = 0, .initialized = false};
static struct device_state ready_state = {.init_res = 0, .initialized = true};

static const struct device not_ready = {
    .name = "not ready",
    .state = &not_ready_state,
};

const struct device *fake_poll_uart_not_ready(void)
{
    return &not_ready;
}

static struct {
    uint8_t sent[SENT_MAX];
    size_t count;
} record;

static void record_poll_out(const struct device *dev, unsigned char c)
{
    (void)dev;
    if (record.count < SENT_MAX) {
        record.sent[record.count] = c;
    }
    record.count++;
}

static DEVICE_API(uart, record_api) = {
    .poll_out = record_poll_out,
};

static const struct device recording = {
    .name = "recording",
    .api = &record_api,
    .state = &ready_state,
};

const struct device *fake_poll_uart(void)
{
    return &recording;
}

void fake_poll_uart_reset(void)
{
    memset(&record, 0, sizeof(record));
}

const uint8_t *fake_poll_uart_sent(void)
{
    return record.sent;
}

size_t fake_poll_uart_sent_count(void)
{
    return record.count;
}
