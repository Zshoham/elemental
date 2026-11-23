/*
 * What only ring_zephyr_uart promises (elemental/ring_zephyr_uart.h),
 * against devices made by hand (fake_uart.h): the failures of setting up,
 * the order in which the driver is driven, what happens to the caller's
 * ring, and the handler's counting, each by an interrupt the test takes
 * itself. ring_zephyr_uart_wire_test.cpp has the receive interrupt of a
 * real device.
 */
#include <gtest/gtest.h>

#include <elemental/io.h>
#include <elemental/ring.h>
#include <elemental/ring_zephyr_uart.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "fake_uart.h"

namespace
{

/* Static: the handler uses them. */
uint8_t storage[64];
el_ring rx;
el_zephyr_uart_irq receiver;

struct Arrivals {
    uint32_t calls;
    size_t len_at_last;       /* what the ring held inside the last call */
    uint32_t dropped_at_last; /* what the count was inside the last call */
};
Arrivals arrivals;

void on_arrival(void *ctx)
{
    Arrivals *const seen = static_cast<Arrivals *>(ctx);
    seen->calls++;
    seen->len_at_last = el_ring_len(&rx);
    seen->dropped_at_last = el_zephyr_uart_irq_dropped(&receiver);
}

std::string numbered(size_t first, size_t count)
{
    std::string bytes;
    for (size_t i = 0; i < count; i++) {
        bytes.push_back(static_cast<char>('A' + (first + i) % 26));
    }
    return bytes;
}

class RingZephyrUartScripted : public testing::Test
{
  protected:
    void SetUp() override
    {
        fake_uart_reset();
        arrivals = Arrivals{};
        el_ring_init(&rx, storage, sizeof(storage));
    }

    void TearDown() override
    {
        if (running) {
            el_zephyr_uart_irq_deinit(&receiver);
        }
        EXPECT_EQ(fake_uart_misuses(), 0u)
            << "an interrupt did not call uart_irq_update() once, before uart_irq_rx_ready()";
    }

    void Start(size_t cap = sizeof(storage), const struct device *dev = fake_uart())
    {
        el_ring_init(&rx, cap > 0 ? storage : nullptr, cap);
        ASSERT_EQ(el_zephyr_uart_irq_init(&receiver, dev, &rx, on_arrival, &arrivals), EL_OK);
        running = true;
    }

    /* Read through the ring's reader until it says anything but EL_OK. */
    std::string Drain(el_status *last = nullptr)
    {
        std::string bytes;
        el_status status;
        do {
            char chunk[32];
            size_t n = 0;
            status = el_read(el_ring_as_reader(&rx), chunk, sizeof(chunk), &n);
            bytes.append(chunk, n);
        } while (status == EL_OK);
        if (last != nullptr) {
            *last = status;
        }
        return bytes;
    }

    bool running = false;
};

TEST_F(RingZephyrUartScripted, AFailedInitLeavesTheRingOpenAndUntouched)
{
    const struct {
        const char *what;
        const struct device *dev;
    } failures[] = {
        {"no device", nullptr},
        {"a device that is not ready", fake_uart_not_ready()},
        {"a driver with no interrupt-driven receive", fake_uart_without_irq()},
    };

    for (const auto &failure : failures) {
        SCOPED_TRACE(failure.what);
        el_ring_init(&rx, storage, sizeof(storage));
        ASSERT_EQ(el_zephyr_uart_irq_init(&receiver, failure.dev, &rx, on_arrival, &arrivals),
                  EL_ERR_IO);

        /* Not closed, and nothing in it: the reader stalls, and does not end. */
        char buf[4];
        size_t n = 1;
        EXPECT_EQ(el_read(el_ring_as_reader(&rx), buf, sizeof(buf), &n), EL_ERR_WOULD_BLOCK);
        EXPECT_EQ(n, 0u);
        EXPECT_EQ(el_ring_len(&rx), 0u);
        EXPECT_EQ(el_ring_room(&rx), sizeof(storage));
        EXPECT_EQ(el_zephyr_uart_irq_dropped(&receiver), 0u);

        /* The producer side is still the caller's: what it writes is read. */
        EXPECT_EQ(el_ring_write(&rx, "ok", 2), 2u);
        EXPECT_EQ(Drain(), "ok");
    }
    EXPECT_EQ(arrivals.calls, 0u);
}

TEST_F(RingZephyrUartScripted, AnAsynchronousReceiverOnTheDeviceIsAnInitFailure)
{
    /* uart_poll_in() reports -EBUSY: the held bytes cannot be discarded. */
    fake_uart_hold("12", 2);
    fake_uart_poll_in_busy();
    ASSERT_EQ(el_zephyr_uart_irq_init(&receiver, fake_uart(), &rx, on_arrival, &arrivals),
              EL_ERR_IO);

    /* Nothing was set up after the discard failed, and the ring is as it was. */
    EXPECT_STREQ(fake_uart_events(), "d");
    EXPECT_FALSE(fake_uart_callback_is_set());
    EXPECT_FALSE(fake_uart_rx_is_enabled());
    char buf[4];
    size_t n = 1;
    EXPECT_EQ(el_read(el_ring_as_reader(&rx), buf, sizeof(buf), &n), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(el_ring_room(&rx), sizeof(storage));
}

TEST_F(RingZephyrUartScripted, WithoutPollInTheBytesTheDeviceHeldAreNotDiscarded)
{
    /* uart_poll_in() reports -ENOSYS: init cannot discard, and goes on. The
     * bytes held are the first the receive interrupt takes. */
    fake_uart_hold("old", 3);
    Start(sizeof(storage), fake_uart_without_poll_in());
    EXPECT_STREQ(fake_uart_events(), "dce");
    EXPECT_EQ(fake_uart_held(), 3u);

    fake_uart_interrupt();
    EXPECT_EQ(arrivals.calls, 1u);
    el_status last = EL_OK;
    EXPECT_EQ(Drain(&last), "old");
    EXPECT_EQ(last, EL_ERR_WOULD_BLOCK);
}

TEST_F(RingZephyrUartScripted, BytesTheCallbackTakesAsItIsSetAreKept)
{
    /* After the discard, bytes arrive and another of the device's interrupts
     * runs the callback as soon as it is set, before the receive interrupt
     * is enabled: the caller's ring is already open. */
    fake_uart_arrive_as_callback_is_set("hey", 3);
    Start();
    EXPECT_STREQ(fake_uart_events(), "dcie");
    EXPECT_EQ(fake_uart_held(), 0u);
    EXPECT_EQ(arrivals.calls, 1u);
    EXPECT_EQ(arrivals.len_at_last, 3u);
    EXPECT_EQ(el_zephyr_uart_irq_dropped(&receiver), 0u);
    EXPECT_EQ(Drain(), "hey");
}

TEST_F(RingZephyrUartScripted, InitDiscardsWhatTheDeviceHeldThenListensAndDeinitStopsListening)
{
    fake_uart_hold("12345", 5);

    Start();
    /* The receive interrupt off, five bytes read and thrown away, then the
     * callback, then the receive interrupt on: the handler is in place before
     * it can run. */
    EXPECT_STREQ(fake_uart_events(), "dpppppce");
    EXPECT_EQ(fake_uart_held(), 0u);
    EXPECT_EQ(el_ring_len(&rx), 0u);
    EXPECT_TRUE(fake_uart_rx_is_enabled());

    el_zephyr_uart_irq_deinit(&receiver);
    running = false;
    /* The interrupt first, then the callback: nothing runs half-removed. */
    EXPECT_STREQ(fake_uart_events(), "dpppppcedx");
    EXPECT_FALSE(fake_uart_rx_is_enabled());
    EXPECT_FALSE(fake_uart_callback_is_set());
}

TEST_F(RingZephyrUartScripted,
       TheHandlerKeepsEveryByteInOrderAndCallsOnArrivalOncePerInterruptThatTookBytes)
{
    Start();

    /* Forty bytes: more than the handler reads at a time. */
    const std::string first = numbered(0, 40);
    fake_uart_hold(first.data(), first.size());
    fake_uart_interrupt();
    EXPECT_EQ(arrivals.calls, 1u);
    EXPECT_EQ(arrivals.len_at_last, 40u) << "called before the bytes were in the ring";

    /* An interrupt that finds nothing tells nobody. */
    fake_uart_interrupt();
    EXPECT_EQ(arrivals.calls, 1u);

    const std::string second = numbered(40, 3);
    fake_uart_hold(second.data(), second.size());
    fake_uart_interrupt();
    EXPECT_EQ(arrivals.calls, 2u);
    EXPECT_EQ(arrivals.len_at_last, 43u);

    EXPECT_EQ(Drain(), first + second);
    EXPECT_EQ(el_zephyr_uart_irq_dropped(&receiver), 0u);
}

TEST_F(RingZephyrUartScripted, ADriverWhoseReadyIsEdgeTriggeredIsReadUntilItsFifoIsEmpty)
{
    Start();
    fake_uart_edge_triggered();

    /* Ready is said once, and forty bytes wait: the handler reads until the
     * FIFO gives less than it asked for. */
    const std::string bytes = numbered(0, 40);
    fake_uart_hold(bytes.data(), bytes.size());
    fake_uart_interrupt();
    EXPECT_EQ(fake_uart_held(), 0u);
    EXPECT_EQ(arrivals.calls, 1u);
    EXPECT_EQ(Drain(), bytes);
}

TEST_F(RingZephyrUartScripted, AFullRingDropsAndCountsBeforeTheApplicationIsTold)
{
    Start(4);

    fake_uart_hold("0123456789", 10);
    fake_uart_interrupt();
    EXPECT_EQ(el_ring_len(&rx), 4u);
    EXPECT_EQ(el_zephyr_uart_irq_dropped(&receiver), 6u);
    /* An interrupt whose bytes were all or partly dropped is still one that
     * took bytes, and the count is in place when it is called. */
    EXPECT_EQ(arrivals.calls, 1u);
    EXPECT_EQ(arrivals.dropped_at_last, 6u);

    /* Nothing is read, so all of the next three are dropped. */
    fake_uart_hold("abc", 3);
    fake_uart_interrupt();
    EXPECT_EQ(el_zephyr_uart_irq_dropped(&receiver), 9u);
    EXPECT_EQ(arrivals.calls, 2u);
    EXPECT_EQ(arrivals.dropped_at_last, 9u);

    EXPECT_EQ(Drain(), "0123");
}

TEST_F(RingZephyrUartScripted, TheDropCountIsReportedModuloTwoToThe32)
{
    /* A ring with no storage: a receiver that drops every byte. The count is
     * set to three short of the 32-bit wrap, as the handler would have left
     * it. */
    Start(0);
    fake_uart_seed_dropped(&receiver, 0xFFFFFFFDu);

    fake_uart_hold("01234", 5);
    fake_uart_interrupt();
    EXPECT_EQ(el_zephyr_uart_irq_dropped(&receiver), 2u);
    EXPECT_EQ(el_ring_len(&rx), 0u);
}

TEST_F(RingZephyrUartScripted, DeinitClosesTheRingAfterWhatWasReceived)
{
    Start();
    fake_uart_hold("abc", 3);
    fake_uart_interrupt();
    ASSERT_EQ(arrivals.calls, 1u);

    el_zephyr_uart_irq_deinit(&receiver);
    running = false;
    /* Closed: the producer side takes nothing more, and the reader ends
     * after the bytes received. */
    EXPECT_EQ(el_ring_room(&rx), 0u);
    EXPECT_EQ(el_ring_write(&rx, "z", 1), 0u);
    el_status last = EL_OK;
    EXPECT_EQ(Drain(&last), "abc");
    EXPECT_EQ(last, EL_ERR_END_OF_STREAM);
}

TEST_F(RingZephyrUartScripted, AnInterruptAfterDeinitIsNotTakenAndOnArrivalIsNotCalled)
{
    Start();
    fake_uart_hold("abc", 3);
    fake_uart_interrupt();
    ASSERT_EQ(arrivals.calls, 1u);

    el_zephyr_uart_irq_deinit(&receiver);
    running = false;
    fake_uart_hold("def", 3);
    fake_uart_interrupt();
    EXPECT_EQ(arrivals.calls, 1u);
    EXPECT_EQ(fake_uart_held(), 3u) << "taken from the device after deinit";
    EXPECT_EQ(Drain(), "abc");
}

} // namespace
