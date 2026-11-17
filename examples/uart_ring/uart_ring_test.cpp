/*
 * The UART example on a target: a receive interrupt fills a ring, this
 * thread reads it through an el_reader, and the handler logs through a
 * deferred sink that this thread drains.
 *
 * Run on mps2/an385 under QEMU, which joins two of the board's UARTs
 * (uart_wire.h): an emulator run. It shows a real exception entry
 * preempting this thread on one core, and the two sides of a ring used
 * from the two contexts. It does not show timing, or two cores
 * (docs/build.md, "What is verified").
 */
#include <gtest/gtest.h>

#include <elemental/io.h>
#include <elemental/log.h>
#include <elemental/log_deferred.h>
#include <elemental/log_mem.h>
#include <elemental/ring.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <zephyr/kernel.h>

#include "uart_reader.h"
#include "uart_wire.h"

namespace
{

/* Static: the handler uses them, and the test runs on a 4 KiB stack. */
uint8_t rx_storage[64];
uint8_t log_ring_storage[1024];
uint8_t log_record_storage[96];
uint8_t kept_storage[1024];
uart_reader rx;
el_ring log_ring;
el_log_deferred_sink deferred;
context_clock isr_clock;
el_logger isr_log;
el_log_mem_sink kept;

class UartRing : public testing::Test
{
  protected:
    void SetUp() override
    {
        dev = uart_wire_receiver();
        ASSERT_NE(dev, nullptr) << "the two UARTs are not ready";

        /* The handler's logger: its records wait in a ring for this
         * thread, and its clock is read in the handler. */
        el_ring_init(&log_ring, log_ring_storage, sizeof(log_ring_storage));
        el_log_deferred_sink_init(&deferred, &log_ring, log_record_storage,
                                  sizeof(log_record_storage));
        context_clock_init(&isr_clock);
        el_logger_init(&isr_log, el_log_deferred_sink_as_sink(&deferred),
                       context_clock_as_clock(&isr_clock), "uart2", EL_LOG_LEVEL_INFO);
        el_log_mem_sink_init(&kept, kept_storage, sizeof(kept_storage));
    }

    void Start(size_t ring_cap)
    {
        uart_reader_start(&rx, dev, rx_storage, ring_cap, &isr_log);
        started = true;
    }

    void TearDown() override
    {
        if (started) {
            uart_reader_stop(&rx);
        }
    }

    /* Read `len` bytes through the reader, waiting on the handler's
     * semaphore whenever the ring is empty. */
    std::string Read(size_t len)
    {
        const el_reader in = uart_reader_as_reader(&rx);
        std::string received(len, '\0');
        size_t total = 0;
        while (total < len) {
            size_t n = 0;
            const el_status status = el_read(in, &received[total], len - total, &n);
            if (status == EL_ERR_WOULD_BLOCK) {
                stalls++;
                if (!uart_reader_wait(&rx, K_MSEC(2000))) {
                    ADD_FAILURE() << "timed out after " << total << " of " << len << " bytes";
                    break;
                }
                continue;
            }
            if (status != EL_OK) {
                ADD_FAILURE() << "el_read: " << el_status_name(status);
                break;
            }
            total += n;
        }
        received.resize(total);
        return received;
    }

    /* Wait until the handler has taken `bytes` bytes from the device. */
    uart_reader_counts WaitForBytes(uint32_t bytes)
    {
        uart_reader_counts counts;
        for (int i = 0; i < 200; i++) {
            uart_reader_counts_get(&rx, &counts);
            if (counts.bytes >= bytes) {
                break;
            }
            k_sleep(K_MSEC(10));
        }
        return counts;
    }

    /* Hand the handler's records on to the memory sink; how many. */
    size_t Drain()
    {
        return el_log_deferred_sink_drain(&deferred, el_log_mem_sink_as_sink(&kept));
    }

    const struct device *dev = nullptr;
    bool started = false;
    unsigned stalls = 0;
};

TEST_F(UartRing, AThreadReadsThroughAReaderWhatTheReceiveInterruptWrote)
{
    Start(sizeof(rx_storage));
    static const char *const lines[] = {"the quick brown fox jumps over the lazy dog\n",
                                        "pack my box with five dozen liquor jugs\n"};

    for (const char *const line : lines) {
        /* Each byte arrives as an interrupt, while this thread is still
         * sending or already reading. */
        uart_wire_send(line, std::strlen(line));
        EXPECT_EQ(Read(std::strlen(line)), line);
    }

    const uint32_t sent = static_cast<uint32_t>(std::strlen(lines[0]) + std::strlen(lines[1]));
    const uart_reader_counts counts = WaitForBytes(sent);
    EXPECT_EQ(counts.bytes, sent);
    EXPECT_EQ(counts.dropped, 0u);
    EXPECT_GT(counts.interrupts, 0u) << "no receive interrupt was taken";
    EXPECT_GT(stalls, 0u) << "the reader never found the ring empty: nothing was waited for";

    /* Nothing more comes: the reader stalls, and does not end. */
    char byte;
    size_t n = 1;
    EXPECT_EQ(el_read(uart_reader_as_reader(&rx), &byte, 1, &n), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(n, 0u);
}

TEST_F(UartRing, TheHandlerLogsThroughADeferredSinkAndAThreadHandsTheRecordsOn)
{
    Start(sizeof(rx_storage));
    const el_ticks before = el_clock_now(context_clock_as_clock(&isr_clock));

    static const char text[] = "one\ntwo\nthree\n";
    uart_wire_send(text, sizeof(text) - 1);
    EXPECT_EQ(Read(sizeof(text) - 1), text);
    (void)WaitForBytes(sizeof(text) - 1);

    /* The handler logged one record at the end of each line, and none of
     * them reached a sink that writes until this thread drained. */
    EXPECT_EQ(el_log_mem_sink_count(&kept), 0u);
    EXPECT_EQ(Drain(), 3u);
    uart_reader_stop(&rx);
    started = false;
    const el_ticks after = el_clock_now(context_clock_as_clock(&isr_clock));

    EXPECT_EQ(el_log_deferred_sink_lost(&deferred), 0u);
    ASSERT_EQ(el_log_mem_sink_count(&kept), 3u);
    el_ticks previous = before;
    for (size_t i = 0; i < 3; i++) {
        el_log_record record;
        el_log_mem_sink_get(&kept, i, &record);
        const std::string message(record.message, record.len);
        EXPECT_EQ(message.rfind("end of line, interrupt ", 0), 0u)
            << "record " << i << ": " << message;
        EXPECT_EQ(record.level, EL_LOG_LEVEL_INFO);
        EXPECT_STREQ(record.tag, "uart2");
        EXPECT_EQ(record.ticks_per_second, static_cast<uint32_t>(CONFIG_SYS_CLOCK_TICKS_PER_SEC));
        EXPECT_GE(record.timestamp, previous) << "record " << i;
        EXPECT_LE(record.timestamp, after) << "record " << i;
        previous = record.timestamp;
    }

    /* Each record's time was read where it was logged: in the handler. The
     * two readings from this thread are the ones above. */
    EXPECT_EQ(isr_clock.from_interrupt, 3u);
    EXPECT_EQ(isr_clock.from_thread, 2u);
}

TEST_F(UartRing, AFullRingDropsWhatDoesNotFitAndTheHandlerSaysSo)
{
    /* Sixteen bytes of ring, twenty-four sent, and nobody reading. */
    Start(16);
    static const char text[] = "0123456789abcdefghijklmn";
    uart_wire_send(text, 24);
    const uart_reader_counts counts = WaitForBytes(24);
    EXPECT_EQ(counts.bytes, 24u);
    EXPECT_EQ(counts.dropped, 8u);

    /* What was kept is the first sixteen, in order. */
    EXPECT_EQ(Read(16), "0123456789abcdef");

    /* The handler did not wait and did not fail: it logged. */
    const size_t records = Drain();
    EXPECT_EQ(el_log_deferred_sink_lost(&deferred), 0u);
    ASSERT_GE(records, 1u);
    unsigned dropped_in_records = 0;
    for (size_t i = 0; i < records; i++) {
        el_log_record record;
        el_log_mem_sink_get(&kept, i, &record);
        EXPECT_EQ(record.level, EL_LOG_LEVEL_WARN);
        const std::string message(record.message, record.len);
        EXPECT_NE(message.find(" bytes dropped, the ring is full"), std::string::npos) << message;
        dropped_in_records += static_cast<unsigned>(std::strtoul(message.c_str(), nullptr, 10));
    }
    EXPECT_EQ(dropped_in_records, 8u);

    /* The room came back: the ring takes bytes again. */
    uart_wire_send("xyz", 3);
    EXPECT_EQ(Read(3), "xyz");
}

} // namespace
