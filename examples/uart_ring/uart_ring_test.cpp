/*
 * The UART example on a target: a UART's receive interrupt fills this
 * program's ring through elemental::ring_zephyr_uart and wakes this
 * thread, which reads it through the ring's el_reader; the polled writer
 * of elemental::io_zephyr_uart sends; an interrupt handler of the
 * application's own logs through a deferred sink that this thread drains.
 * The proposal's Example 1 (docs/proposals/seams.md) is this program, with
 * the receive side as it became after the proposal was accepted: the
 * receiver fills a ring the application owns.
 *
 * Run on mps2/an385 under QEMU, which joins two of the board's UARTs
 * (uart_wire.h): an emulator run. It shows a real exception entry
 * preempting this thread on one core, and the two sides of a ring used
 * from the two contexts. It does not show timing, or two cores
 * (docs/build.md, "What is verified"). The backends' own promises are
 * tested with them, in modules/ring/backends/zephyr_uart/tests and
 * modules/io/backends/zephyr_uart/tests.
 */
#include <gtest/gtest.h>

#include <elemental/io.h>
#include <elemental/io_zephyr_uart.h>
#include <elemental/log.h>
#include <elemental/log_deferred.h>
#include <elemental/log_mem.h>
#include <elemental/log_printf.h>
#include <elemental/ring.h>
#include <elemental/ring_zephyr_uart.h>
#include <elemental/time_zephyr.h>

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <zephyr/kernel.h>

#include "isr_tick.h"
#include "uart_wire.h"

namespace
{

constexpr size_t kMessageMax = 64; /* the longest message an interrupt handler logs */
constexpr size_t kWaiting = 8;     /* records that may wait for this thread */

/* Static: the handlers use them, and the test runs on a 4 KiB stack. */
uint8_t rx_storage[64];
uint8_t deferred_storage[EL_LOG_DEFERRED_SINK_STORAGE_SIZE(kWaiting, kMessageMax)];
uint8_t kept_storage[1024];
el_zephyr_uart_poll sender;
el_ring rx; /* the receive interrupt writes, this thread reads */
el_zephyr_uart_irq sensor;
struct k_sem arrived;
el_log_deferred_sink deferred;
context_clock isr_clock;
el_logger isr_log; /* given to the application's interrupt handlers */
el_logger app_log; /* this thread's, straight into the sink that keeps */
el_log_mem_sink kept;
isr_tick tick;

/* Runs in the sensor UART's receive interrupt. */
void on_arrival(void *ctx)
{
    k_sem_give(static_cast<struct k_sem *>(ctx));
}

class UartRing : public testing::Test
{
  protected:
    void SetUp() override
    {
        k_sem_init(&arrived, 0, 1);
        ASSERT_EQ(el_zephyr_uart_poll_init(&sender, uart_wire_sender()), EL_OK)
            << "the two UARTs are not ready";

        /* The handlers' logger: its records wait in the sink's queue for
         * this thread, and its clock is read in the handler. */
        el_log_deferred_sink_init(&deferred, deferred_storage, sizeof(deferred_storage),
                                  kMessageMax);
        context_clock_init(&isr_clock);
        el_logger_init(&isr_log, el_log_deferred_sink_as_sink(&deferred),
                       context_clock_as_clock(&isr_clock), "isr", EL_LOG_LEVEL_INFO);
        el_log_mem_sink_init(&kept, kept_storage, sizeof(kept_storage));
        el_logger_init(&app_log, el_log_mem_sink_as_sink(&kept), el_zephyr_clock(), "app",
                       EL_LOG_LEVEL_INFO);
    }

    void Start(size_t ring_cap)
    {
        el_ring_init(&rx, rx_storage, ring_cap);
        ASSERT_EQ(el_zephyr_uart_irq_init(&sensor, uart_wire_receiver(), &rx, on_arrival, &arrived),
                  EL_OK);
        started = true;
    }

    void TearDown() override
    {
        if (ticking) {
            isr_tick_stop(&tick);
        }
        if (started) {
            el_zephyr_uart_irq_deinit(&sensor);
        }
    }

    void Send(const char *text)
    {
        ASSERT_EQ(
            el_write_all(el_zephyr_uart_poll_as_writer(&sender), text, std::strlen(text), nullptr),
            EL_OK);
    }

    /* Read `len` bytes through the reader, waiting for the handler's
     * word whenever the ring is empty: the example's loop. */
    std::string Read(size_t len)
    {
        const el_reader in = el_ring_as_reader(&rx);
        std::string received(len, '\0');
        size_t total = 0;
        while (total < len) {
            size_t n = 0;
            const el_status status = el_read(in, &received[total], len - total, &n);
            if (status == EL_ERR_WOULD_BLOCK) {
                if (k_sem_take(&arrived, K_MSEC(2000)) != 0) {
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

    /* The example's report: what the handler dropped since the last look. */
    uint32_t ReportDrops(uint32_t seen)
    {
        const uint32_t now = el_zephyr_uart_irq_dropped(&sensor);
        if (now != seen) {
            EL_LOG_WARN(&app_log, "%" PRIu32 " bytes dropped, the receive ring was full",
                        now - seen);
        }
        return now;
    }

    bool started = false;
    bool ticking = false;
};

TEST_F(UartRing, AThreadReadsThroughAReaderWhatTheReceiveInterruptWrote)
{
    Start(sizeof(rx_storage));
    const el_reader in = el_ring_as_reader(&rx);

    /* Nothing has come: the reader stalls, and does not end. */
    char byte = 0;
    size_t n = 1;
    EXPECT_EQ(el_read(in, &byte, 1, &n), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(n, 0u);

    /* One byte. The interrupt stores it and gives the semaphore, and the
     * thread that takes the semaphore then reads it. The give may come
     * before the take: the semaphore keeps it. */
    Send("!");
    const int taken = k_sem_take(&arrived, K_MSEC(2000));
    ASSERT_EQ(taken, 0) << "on_arrival was not called";
    ASSERT_EQ(el_read(in, &byte, 1, &n), EL_OK);
    EXPECT_EQ(n, 1u);
    EXPECT_EQ(byte, '!');

    /* Whole lines, read as the example reads: every byte, in order. */
    static const char *const lines[] = {"the quick brown fox jumps over the lazy dog\n",
                                        "pack my box with five dozen liquor jugs\n"};
    for (const char *const line : lines) {
        Send(line);
        EXPECT_EQ(Read(std::strlen(line)), line);
    }
    EXPECT_EQ(el_zephyr_uart_irq_dropped(&sensor), 0u);

    /* Nothing more comes: the reader stalls again. */
    n = 1;
    EXPECT_EQ(el_read(in, &byte, 1, &n), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(n, 0u);
}

TEST_F(UartRing, AnInterruptHandlerLogsThroughADeferredSinkAndAThreadHandsTheRecordsOn)
{
    const el_ticks before = el_clock_now(context_clock_as_clock(&isr_clock));

    /* Three ticks of a timer, in its interrupt, while this thread waits. */
    isr_tick_start(&tick, &isr_log, 3, 10);
    ticking = true;
    ASSERT_TRUE(isr_tick_wait(&tick, K_MSEC(2000))) << "the timer did not tick three times";
    const el_ticks after = el_clock_now(context_clock_as_clock(&isr_clock));

    /* None of the records reached a sink that writes until this thread
     * drained. */
    EXPECT_EQ(el_log_mem_sink_count(&kept), 0u);
    EXPECT_EQ(el_log_deferred_sink_drain(&deferred, el_log_mem_sink_as_sink(&kept)), 3u);

    EXPECT_EQ(el_log_deferred_sink_lost(&deferred), 0u);
    ASSERT_EQ(el_log_mem_sink_count(&kept), 3u);
    el_ticks previous = before;
    for (size_t i = 0; i < 3; i++) {
        el_log_record record;
        el_log_mem_sink_get(&kept, i, &record);
        EXPECT_EQ(std::string(record.message, record.len),
                  "tick " + std::to_string(i + 1) + " of 3");
        EXPECT_EQ(record.level, EL_LOG_LEVEL_INFO);
        EXPECT_STREQ(record.tag, "isr");
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

TEST_F(UartRing, TheThreadReportsWhatTheReceiveInterruptDropped)
{
    /* Sixteen bytes of ring, twenty-four sent, and nobody reading. */
    Start(16);
    uint32_t seen = 0;
    Send("0123456789abcdefghijklmn");
    for (int i = 0; i < 400 && el_zephyr_uart_irq_dropped(&sensor) < 8; i++) {
        k_sleep(K_MSEC(5));
    }
    seen = ReportDrops(seen);
    EXPECT_EQ(seen, 8u);

    /* What was kept is the first sixteen, in order. */
    EXPECT_EQ(Read(16), "0123456789abcdef");

    /* The report is one warning, and says how many. */
    ASSERT_EQ(el_log_mem_sink_count(&kept), 1u);
    el_log_record record;
    el_log_mem_sink_get(&kept, 0, &record);
    EXPECT_EQ(record.level, EL_LOG_LEVEL_WARN);
    EXPECT_EQ(std::string(record.message, record.len),
              "8 bytes dropped, the receive ring was full");

    /* Nothing new was dropped, so the next look says nothing. */
    EXPECT_EQ(ReportDrops(seen), seen);
    EXPECT_EQ(el_log_mem_sink_count(&kept), 1u);
}

} // namespace
