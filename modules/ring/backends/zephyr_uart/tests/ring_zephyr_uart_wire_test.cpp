/*
 * What only ring_zephyr_uart promises (elemental/ring_zephyr_uart.h), and
 * what io_zephyr_uart's polled writer promises of a real device
 * (elemental/io_zephyr_uart.h), over the emulator's wire: uart1's output
 * is uart2's input, the polled writer sends on uart1 and the receiver fills
 * a ring from uart2, in a real receive interrupt. See
 * ring_zephyr_uart_test.cpp for what a scripted device can stage that the
 * wire cannot.
 */
#include <gtest/gtest.h>

#include <elemental/io.h>
#include <elemental/io_zephyr_uart.h>
#include <elemental/ring.h>
#include <elemental/ring_zephyr_uart.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include <zephyr/kernel.h>

#include "wire.h"

namespace
{

/* Static: the receive handler uses them, and the test runs on a 4 KiB stack. */
uint8_t storage[256];
el_ring rx;
el_zephyr_uart_poll sender;
el_zephyr_uart_irq receiver;
struct k_sem arrived;

/* What on_arrival saw. The handler writes it, the test reads it. */
struct Arrivals {
    volatile uint32_t calls;
    volatile uint32_t in_interrupt; /* calls made in interrupt context */
    volatile uint32_t empty;        /* calls that found nothing in the ring */
    volatile size_t len_at_last;    /* what the ring held inside the last call */
};
Arrivals arrivals;

void on_arrival(void *ctx)
{
    Arrivals *const seen = static_cast<Arrivals *>(ctx);
    const size_t len = el_ring_len(&rx);
    seen->calls = seen->calls + 1;
    seen->in_interrupt = seen->in_interrupt + (k_is_in_isr() ? 1u : 0u);
    seen->empty = seen->empty + (len == 0 ? 1u : 0u);
    seen->len_at_last = len;
    k_sem_give(&arrived);
}

class RingZephyrUartWire : public testing::Test
{
  protected:
    void SetUp() override
    {
        zephyr_wire_flush();
        arrivals.calls = 0;
        arrivals.in_interrupt = 0;
        arrivals.empty = 0;
        arrivals.len_at_last = 0;
        k_sem_init(&arrived, 0, 1);
        ASSERT_EQ(el_zephyr_uart_poll_init(&sender, zephyr_wire_sender()), EL_OK);
    }

    void TearDown() override
    {
        if (running) {
            Stop();
        }
    }

    void Start(size_t cap = sizeof(storage))
    {
        el_ring_init(&rx, storage, cap);
        ASSERT_EQ(
            el_zephyr_uart_irq_init(&receiver, zephyr_wire_receiver(), &rx, on_arrival, &arrivals),
            EL_OK);
        running = true;
    }

    void Stop()
    {
        el_zephyr_uart_irq_deinit(&receiver);
        running = false;
    }

    /* Send through the polled writer, in one write. */
    void Send(const std::string &bytes)
    {
        size_t n = 0;
        ASSERT_EQ(el_write(el_zephyr_uart_poll_as_writer(&sender), bytes.data(), bytes.size(), &n),
                  EL_OK);
        ASSERT_EQ(n, bytes.size());
    }

    /* Until the receiver holds `len` bytes: they arrive in interrupts. */
    bool WaitForLen(size_t len)
    {
        for (int i = 0; i < 400 && el_ring_len(&rx) < len; i++) {
            k_sleep(K_MSEC(5));
        }
        return el_ring_len(&rx) >= len;
    }

    /* Read until the reader says anything but EL_OK. */
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

TEST_F(RingZephyrUartWire, ThePolledWriterTakesEveryByteItIsGiven)
{
    Start();
    std::string text;
    for (int i = 0; i < 100; i++) {
        text.push_back(static_cast<char>('a' + i % 26));
    }

    size_t n = 0;
    EXPECT_EQ(el_write(el_zephyr_uart_poll_as_writer(&sender), text.data(), text.size(), &n),
              EL_OK);
    EXPECT_EQ(n, text.size());

    /* All of it arrives, in order, on the other end of the wire. */
    ASSERT_TRUE(WaitForLen(text.size())) << "received " << el_ring_len(&rx);
    EXPECT_EQ(Drain(), text);
    EXPECT_EQ(el_zephyr_uart_irq_dropped(&receiver), 0u);
}

TEST_F(RingZephyrUartWire, BytesTheDeviceHeldBeforeInitAreDiscarded)
{
    /* One byte, because the device holds one: it arrives while nobody is
     * listening and waits in the device. */
    Send("X");
    k_sleep(K_MSEC(100));

    Start();
    Send("ABC");
    ASSERT_TRUE(WaitForLen(3));
    k_sleep(K_MSEC(50));
    EXPECT_EQ(Drain(), "ABC");
    EXPECT_EQ(el_zephyr_uart_irq_dropped(&receiver), 0u);
}

TEST_F(RingZephyrUartWire, AFullRingDropsWhatDoesNotFitAndCountsIt)
{
    /* Sixteen bytes of ring, twenty-four sent, and nobody reading. */
    Start(16);
    Send("0123456789abcdefghijklmn");
    for (int i = 0; i < 400 && el_zephyr_uart_irq_dropped(&receiver) < 8; i++) {
        k_sleep(K_MSEC(5));
    }
    EXPECT_EQ(el_zephyr_uart_irq_dropped(&receiver), 8u);
    EXPECT_EQ(el_ring_len(&rx), 16u);

    /* What was kept is the first sixteen, in order. */
    EXPECT_EQ(Drain(), "0123456789abcdef");

    /* The room came back: the ring takes bytes again, and the count stays. */
    Send("xyz");
    ASSERT_TRUE(WaitForLen(3));
    EXPECT_EQ(Drain(), "xyz");
    EXPECT_EQ(el_zephyr_uart_irq_dropped(&receiver), 8u);
}

TEST_F(RingZephyrUartWire, OnArrivalIsCalledInTheInterruptAfterTheBytesAreInTheRing)
{
    Start();
    Send("hello");
    ASSERT_TRUE(WaitForLen(5));

    EXPECT_GE(arrivals.calls, 1u);
    EXPECT_LE(arrivals.calls, 5u) << "more than once per byte";
    EXPECT_EQ(arrivals.in_interrupt, arrivals.calls) << "called outside the receive interrupt";
    /* Every call found bytes in the ring, and the last one found them all:
     * the bytes are stored before the application is told. */
    EXPECT_EQ(arrivals.empty, 0u);
    EXPECT_EQ(arrivals.len_at_last, 5u);
    EXPECT_EQ(k_sem_count_get(&arrived), 1u);
}

TEST_F(RingZephyrUartWire, OnArrivalIsNotCalledAfterDeinitReturns)
{
    Start();
    Send("abc");
    ASSERT_TRUE(WaitForLen(3));
    Stop();

    const uint32_t calls = arrivals.calls;
    const size_t len = el_ring_len(&rx);
    EXPECT_EQ(len, 3u);

    /* One byte: the device holds it, and nothing may take it. It is there
     * to be polled from the device, the same byte. */
    Send("d");
    unsigned char byte = 0;
    ASSERT_TRUE(zephyr_wire_poll_receive(&byte, 2000)) << "the byte is not in the device";
    EXPECT_EQ(byte, 'd');
    EXPECT_EQ(arrivals.calls, calls);
    EXPECT_EQ(el_ring_len(&rx), len);
}

TEST_F(RingZephyrUartWire, DeinitEndsTheReaderAfterWhatWasReceived)
{
    Start();
    const el_reader in = el_ring_as_reader(&rx);
    char buf[3];
    size_t n = 1;

    /* Running and empty is not an end. */
    EXPECT_EQ(el_read(in, buf, sizeof(buf), &n), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(n, 0u);

    Send("wxyz");
    ASSERT_TRUE(WaitForLen(4));
    Stop();

    /* The four bytes received, then the end, and the end for good. */
    ASSERT_EQ(el_read(in, buf, sizeof(buf), &n), EL_OK);
    EXPECT_EQ(std::string(buf, n), "wxy");
    ASSERT_EQ(el_read(in, buf, sizeof(buf), &n), EL_OK);
    EXPECT_EQ(std::string(buf, n), "z");
    for (int i = 0; i < 3; i++) {
        n = 1;
        EXPECT_EQ(el_read(in, buf, sizeof(buf), &n), EL_ERR_END_OF_STREAM);
        EXPECT_EQ(n, 0u);
    }
    EXPECT_EQ(el_ring_len(&rx), 0u);
}

} // namespace
