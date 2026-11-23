/*
 * The deferred sink with a context on each side: one thread logs while
 * another drains (elemental/log_deferred.h). Host only: it needs threads.
 *
 * Every record that arrives must be whole and in the order logged
 * (stress_support.c checks each), and every record logged must have arrived
 * or been counted as lost. As for the
 * ring's own stress test (modules/ring/tests/ring_stress_test.cpp), the run
 * that can fail on a data race is the one under ThreadSanitizer, the `tsan`
 * preset.
 */
#include <gtest/gtest.h>

#include <elemental/log.h>
#include <elemental/log_deferred.h>
#include <elemental/log_printf.h>
#include <elemental/time_manual.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "stress_support.h"

namespace
{

class ElLogDeferredStress : public testing::TestWithParam<size_t>
{
};

TEST_P(ElLogDeferredStress, RecordsCrossBetweenTwoThreadsWholeAndInOrderOrAreCounted)
{
    constexpr unsigned kRecords = 100000;
    /* The longest message below, "record 99999 xxxxxxxxxxxxxxxx". */
    constexpr size_t kMessageMax = 32;

    /* The queue is the parameter; the record area comes before it. */
    std::vector<uint8_t> storage(el_log_deferred_sink_record_size(kMessageMax) + GetParam());
    el_log_deferred_sink sink;
    el_log_deferred_sink_init(&sink, storage.data(), storage.size(), kMessageMax);

    sequence_sink checking;
    sequence_sink_init(&checking);

    std::atomic<bool> done{false};
    std::thread logging([&sink, &done] {
        el_manual_clock manual;
        el_manual_clock_init(&manual, 1000);
        el_logger log;
        el_logger_init(&log, el_log_deferred_sink_as_sink(&sink), el_manual_clock_as_clock(&manual),
                       "stress", EL_LOG_LEVEL_INFO);
        for (unsigned i = 0; i < kRecords; i++) {
            el_manual_clock_advance(&manual, 1);
            /* Messages of every length from 8 to 28 bytes. */
            EL_LOG_INFO(&log, "record %u %.*s", i, static_cast<int>(i % 17), "xxxxxxxxxxxxxxxx");
            if (i % 64 == 0) {
                std::this_thread::yield();
            }
        }
        done.store(true);
    });

    /* This thread writes. The lost count may be read from any context. */
    size_t drained = 0;
    uint32_t lost_seen = 0;
    bool lost_went_back = false;
    for (;;) {
        const bool was_done = done.load();
        drained += el_log_deferred_sink_drain(&sink, sequence_sink_as_sink(&checking));
        const uint32_t lost = el_log_deferred_sink_lost(&sink);
        lost_went_back = lost_went_back || lost < lost_seen;
        lost_seen = lost;
        if (was_done) {
            /* Drained after the last record was logged: nothing is left. */
            break;
        }
    }
    logging.join();

    const uint32_t lost = el_log_deferred_sink_lost(&sink);
    EXPECT_FALSE(lost_went_back);
    EXPECT_EQ(drained, checking.count);
    EXPECT_EQ(checking.count + lost, kRecords)
        << checking.count << " arrived, " << lost << " counted as lost";
    EXPECT_GT(checking.count, 0u);
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), 0u);
    EXPECT_EQ(checking.wrong, 0u) << "records arrived changed or out of order; the first: \""
                                  << checking.first_wrong << "\"";
}

/* A queue a few records long, which fills and loses; and one that holds
 * many, where most of the work is the copying. */
INSTANTIATE_TEST_SUITE_P(QueueSizes, ElLogDeferredStress, testing::Values(200u, 4096u),
                         [](const testing::TestParamInfo<size_t> &param_info) {
                             return "Queue" + std::to_string(param_info.param);
                         });

} // namespace
