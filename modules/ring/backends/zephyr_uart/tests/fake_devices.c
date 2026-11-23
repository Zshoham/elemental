#include "fake_uart.h"

#include <errno.h>
#include <stdatomic.h>
#include <string.h>

#include <zephyr/drivers/uart.h>

static struct device_state not_ready_state = {.init_res = 0, .initialized = false};
static struct device_state ready_state = {.init_res = 0, .initialized = true};

/* ---- A device that is not ready ----------------------------------------- */

static const struct device not_ready = {
    .name = "not ready",
    .state = &not_ready_state,
};

const struct device *fake_uart_not_ready(void)
{
    return &not_ready;
}

/* ---- A driver with no interrupt callback -------------------------------- */

static int no_irq_poll_in(const struct device *dev, unsigned char *c)
{
    (void)dev;
    (void)c;
    return -1;
}

static DEVICE_API(uart, no_irq_api) = {
    .poll_in = no_irq_poll_in,
};

static const struct device without_irq = {
    .name = "without irq",
    .api = &no_irq_api,
    .state = &ready_state,
};

const struct device *fake_uart_without_irq(void)
{
    return &without_irq;
}

/* ---- The scripted device ------------------------------------------------ */

#define HELD_MAX 64
#define EVENTS_MAX 32

static struct {
    uint8_t held[HELD_MAX];
    size_t held_len;
    size_t held_pos;
    uint8_t late[HELD_MAX]; /* arrive as a callback is set */
    size_t late_len;
    uart_irq_callback_user_data_t callback;
    void *callback_data;
    const struct device *callback_dev;
    int poll_in_busy; /* poll_in reports -EBUSY */
    int rx_enabled;
    int edge_triggered; /* rx_ready says yes once per interrupt */
    /* One run of the callback, as the driver's interrupt runs it: */
    int in_interrupt;
    int rx_ready_armed; /* edge-triggered: this interrupt's one "ready" */
    int updates;        /* uart_irq_update() calls in this run */
    int latched;        /* the receive status uart_irq_update() latched */
    unsigned misuses;
    char events[EVENTS_MAX];
    size_t event_count;
} script;

static void event(char what)
{
    if (script.event_count < EVENTS_MAX - 1) {
        script.events[script.event_count++] = what;
        script.events[script.event_count] = '\0';
    }
}

/* One run of the callback, as from the device's interrupt: the status is
 * stale until the handler calls uart_irq_update(), once. */
static void invoke(void)
{
    event('i');
    script.in_interrupt = 1;
    script.rx_ready_armed = 1;
    script.updates = 0;
    script.latched = 0;
    script.callback(script.callback_dev, script.callback_data);
    if (script.updates != 1) {
        script.misuses++;
    }
    script.in_interrupt = 0;
}

static int script_poll_in(const struct device *dev, unsigned char *c)
{
    (void)dev;
    if (script.poll_in_busy) {
        return -EBUSY;
    }
    if (script.held_pos >= script.held_len) {
        return -1;
    }
    *c = script.held[script.held_pos++];
    event('p');
    return 0;
}

static void script_callback_set(const struct device *dev, uart_irq_callback_user_data_t cb,
                                void *data)
{
    script.callback = cb;
    script.callback_data = data;
    script.callback_dev = dev;
    event(cb != NULL ? 'c' : 'x');
    if (cb != NULL && script.late_len > 0) {
        /* Bytes arrive, and an interrupt that is not the receive one (a
         * transmission completing) runs the callback just installed. */
        fake_uart_hold(script.late, script.late_len);
        script.late_len = 0;
        invoke();
    }
}

static void script_rx_enable(const struct device *dev)
{
    (void)dev;
    script.rx_enabled = 1;
    event('e');
}

static void script_rx_disable(const struct device *dev)
{
    (void)dev;
    script.rx_enabled = 0;
    event('d');
}

static void script_update(const struct device *dev)
{
    (void)dev;
    script.updates++;
    const int pending = script.held_pos < script.held_len;
    if (script.edge_triggered) {
        script.latched = script.rx_ready_armed && pending;
        script.rx_ready_armed = 0;
    } else {
        script.latched = pending;
    }
}

static int script_rx_ready(const struct device *dev)
{
    (void)dev;
    if (!script.in_interrupt || script.updates == 0) {
        script.misuses++;
    }
    const int ready = script.latched;
    if (script.edge_triggered) {
        script.latched = 0;
    }
    return ready;
}

static int script_fifo_read(const struct device *dev, uint8_t *buf, const int size)
{
    (void)dev;
    size_t n = script.held_len - script.held_pos;
    if (n > (size_t)size) {
        n = (size_t)size;
    }
    memcpy(buf, &script.held[script.held_pos], n);
    script.held_pos += n;
    return (int)n;
}

static DEVICE_API(uart, script_api) = {
    .poll_in = script_poll_in,
    .fifo_read = script_fifo_read,
    .irq_rx_enable = script_rx_enable,
    .irq_rx_disable = script_rx_disable,
    .irq_rx_ready = script_rx_ready,
    .irq_update = script_update,
    .irq_callback_set = script_callback_set,
};

static const struct device scripted = {
    .name = "scripted",
    .api = &script_api,
    .state = &ready_state,
};

/* The same device, with a driver that has no poll_in. */
static DEVICE_API(uart, script_no_poll_in_api) = {
    .fifo_read = script_fifo_read,
    .irq_rx_enable = script_rx_enable,
    .irq_rx_disable = script_rx_disable,
    .irq_rx_ready = script_rx_ready,
    .irq_update = script_update,
    .irq_callback_set = script_callback_set,
};

static const struct device scripted_no_poll_in = {
    .name = "scripted, no poll_in",
    .api = &script_no_poll_in_api,
    .state = &ready_state,
};

const struct device *fake_uart(void)
{
    return &scripted;
}

const struct device *fake_uart_without_poll_in(void)
{
    return &scripted_no_poll_in;
}

void fake_uart_reset(void)
{
    memset(&script, 0, sizeof(script));
}

void fake_uart_hold(const void *bytes, size_t len)
{
    memcpy(script.held, bytes, len);
    script.held_len = len;
    script.held_pos = 0;
}

void fake_uart_arrive_as_callback_is_set(const void *bytes, size_t len)
{
    memcpy(script.late, bytes, len);
    script.late_len = len;
}

void fake_uart_poll_in_busy(void)
{
    script.poll_in_busy = 1;
}

size_t fake_uart_held(void)
{
    return script.held_len - script.held_pos;
}

void fake_uart_edge_triggered(void)
{
    script.edge_triggered = 1;
}

void fake_uart_interrupt(void)
{
    if (script.rx_enabled && script.callback != NULL) {
        invoke();
    }
}

unsigned fake_uart_misuses(void)
{
    return script.misuses;
}

int fake_uart_callback_is_set(void)
{
    return script.callback != NULL;
}

int fake_uart_rx_is_enabled(void)
{
    return script.rx_enabled;
}

const char *fake_uart_events(void)
{
    return script.events;
}

void fake_uart_seed_dropped(el_zephyr_uart_irq *uart, size_t count)
{
    atomic_store_explicit(&uart->dropped, count, memory_order_relaxed);
}
