/*
 * Contract violations are caught: callers that break a precondition of the
 * logger, of el_log_sink_write(), or of a sink.
 *
 * Death tests: each violation runs in a child process, and the test passes
 * if that process is stopped by the check, in whichever way the assertion
 * configuration stops it.
 */
#include <gtest/gtest.h>

#include <elemental/log.h>
#include <elemental/log_mem.h>
#include <elemental/log_printf.h>
#include <elemental/log_writer.h>
#include <elemental/time_manual.h>

#include <cstddef>
#include <cstdint>

#include "log_support.h"

#include <csignal>
#include <sys/wait.h>

#if __has_include(<sys/resource.h>)
#include <sys/resource.h>
#define HAVE_RLIMIT 1
#endif
#if __has_include(<sys/prctl.h>)
#include <sys/prctl.h>
#define HAVE_PRCTL 1
#endif

namespace
{

/*
 * Stopped by the check, not by a crash: a failed check traps (SIGILL on
 * this host) or calls the test handler, which aborts (SIGABRT). Without the
 * check, a null pointer would still end the program here, of SIGSEGV, or be
 * reported by AddressSanitizer, which exits; neither is accepted. On a
 * Cortex-M without memory protection the same access would not fault at
 * all, so the check is what these tests are about.
 */
bool StoppedByACheck(int status)
{
    return WIFSIGNALED(status) && (WTERMSIG(status) == SIGILL || WTERMSIG(status) == SIGTRAP ||
                                   WTERMSIG(status) == SIGABRT);
}

#define EXPECT_VIOLATION(statement) EXPECT_EXIT(statement, StoppedByACheck, "")

class LogViolations : public testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        /* A failed check kills the child with a signal: leave neither a core
         * file nor a crash record behind. */
#ifdef HAVE_RLIMIT
        const rlimit no_core = {0, 0};
        setrlimit(RLIMIT_CORE, &no_core);
#endif
#ifdef HAVE_PRCTL
        prctl(PR_SET_DUMPABLE, 0);
#endif
    }

    void SetUp() override
    {
        recording_sink_init(&recording);
        sink = recording_sink_as_sink(&recording);
        el_manual_clock_init(&manual, 1000);
        clock = el_manual_clock_as_clock(&manual);
        el_logger_init(&log, sink, clock, "tag", EL_LOG_LEVEL_DEBUG);
        record.timestamp = 0;
        record.ticks_per_second = 1;
        record.level = EL_LOG_LEVEL_INFO;
        record.tag = "tag";
        record.message = "m";
        record.len = 1;
        el_log_mem_sink_init(&mem, storage, sizeof(storage));
        scripted_writer_init(&writer);
        el_log_writer_sink_init(&writer_sink, scripted_writer_as_writer(&writer), line,
                                sizeof(line));
    }

    recording_sink recording;
    el_log_sink sink;
    el_manual_clock manual;
    el_clock clock;
    el_logger log;
    el_log_record record;
    uint8_t storage[128];
    el_log_mem_sink mem;
    scripted_writer writer;
    char line[64];
    el_log_writer_sink writer_sink;
};

/* GoogleTest runs suites named *DeathTest first, before any threads exist. */
using LogViolationsDeathTest = LogViolations;

TEST_F(LogViolations, CorrectUseDoesNotTripACheck)
{
    /* Including the edge cases. A check that fired would end the test. */
    el_log_write(&log, EL_LOG_LEVEL_DEBUG, nullptr, 0);
    el_log_write(&log, EL_LOG_LEVEL_ERROR, "", 0);
    el_logger_init(&log, sink, clock, "", EL_LOG_LEVEL_OFF);
    el_logger_set_level(&log, EL_LOG_LEVEL_OFF);
    el_log_printf(&log, EL_LOG_LEVEL_ERROR, "%s", "not made");
    record.message = nullptr;
    record.len = 0;
    record.tag = "";
    record.level = EL_LOG_LEVEL_ERROR;
    EXPECT_EQ(el_log_sink_write(sink, &record), EL_OK);
    el_log_mem_sink_init(&mem, nullptr, 0);
    EXPECT_EQ(el_log_mem_sink_record_size(0) + 1, el_log_mem_sink_record_size(1));
    el_log_writer_sink_init(&writer_sink, scripted_writer_as_writer(&writer), nullptr, 0);
    EXPECT_EQ(recording.calls, 3u);
}

TEST_F(LogViolationsDeathTest, CallerLogsThroughANullLogger)
{
    EXPECT_VIOLATION(el_log_write(nullptr, EL_LOG_LEVEL_INFO, "m", 1));
    EXPECT_VIOLATION(el_log_printf(nullptr, EL_LOG_LEVEL_INFO, "m"));
    EXPECT_VIOLATION(el_logger_set_level(nullptr, EL_LOG_LEVEL_INFO));
    EXPECT_VIOLATION(el_logger_init(nullptr, sink, clock, "tag", EL_LOG_LEVEL_INFO));
}

TEST_F(LogViolationsDeathTest, CallerLogsAtALevelThatIsNotARecordsLevel)
{
    EXPECT_VIOLATION(el_log_write(&log, EL_LOG_LEVEL_OFF, "m", 1));
    EXPECT_VIOLATION(el_log_write(&log, 200, "m", 1));
    EXPECT_VIOLATION(el_log_printf(&log, EL_LOG_LEVEL_OFF, "m"));
}

TEST_F(LogViolationsDeathTest, CallerPassesANullMessageWithALength)
{
    EXPECT_VIOLATION(el_log_write(&log, EL_LOG_LEVEL_INFO, nullptr, 1));

    /* Caught even when the record would not be made. */
    el_logger_set_level(&log, EL_LOG_LEVEL_OFF);
    EXPECT_VIOLATION(el_log_write(&log, EL_LOG_LEVEL_INFO, nullptr, 1));
}

TEST_F(LogViolationsDeathTest, CallerSetsALoggerUpWithoutASinkATagOrAValidLevel)
{
    EXPECT_VIOLATION(
        el_logger_init(&log, el_log_sink{nullptr, nullptr}, clock, "tag", EL_LOG_LEVEL_INFO));
    EXPECT_VIOLATION(el_logger_init(&log, sink, clock, nullptr, EL_LOG_LEVEL_INFO));
    EXPECT_VIOLATION(el_logger_init(&log, sink, clock, "tag", EL_LOG_LEVEL_OFF + 1));
    EXPECT_VIOLATION(el_logger_set_level(&log, EL_LOG_LEVEL_OFF + 1));
}

TEST_F(LogViolationsDeathTest, CallerSetsALoggerUpWithAClockThatWasNeverMade)
{
    EXPECT_VIOLATION(
        el_logger_init(&log, sink, el_clock{nullptr, nullptr}, "tag", EL_LOG_LEVEL_INFO));
}

TEST_F(LogViolationsDeathTest, CallerGivesASinkHandleThatWasNeverMade)
{
    EXPECT_VIOLATION((void)el_log_sink_write(el_log_sink{nullptr, nullptr}, &record));
    EXPECT_VIOLATION((void)el_log_sink_write(recording_sink_without_write(&recording), &record));
}

TEST_F(LogViolationsDeathTest, CallerGivesASinkARecordThatBreaksTheRecordsRules)
{
    EXPECT_VIOLATION((void)el_log_sink_write(sink, nullptr));

    el_log_record bad = record;
    bad.level = EL_LOG_LEVEL_OFF;
    EXPECT_VIOLATION((void)el_log_sink_write(sink, &bad));

    bad = record;
    bad.ticks_per_second = 0;
    EXPECT_VIOLATION((void)el_log_sink_write(sink, &bad));

    bad = record;
    bad.tag = nullptr;
    EXPECT_VIOLATION((void)el_log_sink_write(sink, &bad));

    /* Through a sink that refuses before it reads the message, so that
     * only the check can stop the program. */
    el_log_mem_sink_init(&mem, nullptr, 0);
    bad = record;
    bad.message = nullptr;
    EXPECT_VIOLATION((void)el_log_sink_write(el_log_mem_sink_as_sink(&mem), &bad));
}

TEST_F(LogViolationsDeathTest, CallerMisusesTheMemorySink)
{
    EXPECT_VIOLATION(el_log_mem_sink_init(nullptr, storage, sizeof(storage)));
    EXPECT_VIOLATION(el_log_mem_sink_init(&mem, nullptr, 1));
    EXPECT_VIOLATION((void)el_log_mem_sink_as_sink(nullptr));
    EXPECT_VIOLATION((void)el_log_mem_sink_count(nullptr));
    EXPECT_VIOLATION((void)el_log_mem_sink_lost(nullptr));
    EXPECT_VIOLATION(el_log_mem_sink_clear(nullptr));
    EXPECT_VIOLATION((void)el_log_mem_sink_record_size(SIZE_MAX));
}

TEST_F(LogViolationsDeathTest, CallerReadsAMemorySinkRecordThatIsNotThere)
{
    el_log_record out;
    EXPECT_VIOLATION(el_log_mem_sink_get(&mem, 0, &out));
    ASSERT_EQ(el_log_sink_write(el_log_mem_sink_as_sink(&mem), &record), EL_OK);
    EXPECT_VIOLATION(el_log_mem_sink_get(&mem, 1, &out));
    EXPECT_VIOLATION(el_log_mem_sink_get(&mem, 0, nullptr));
    EXPECT_VIOLATION(el_log_mem_sink_get(nullptr, 0, &out));
}

TEST_F(LogViolationsDeathTest, CallerMisusesTheWriterSink)
{
    const el_writer out = scripted_writer_as_writer(&writer);
    EXPECT_VIOLATION(el_log_writer_sink_init(nullptr, out, line, sizeof(line)));
    EXPECT_VIOLATION(
        el_log_writer_sink_init(&writer_sink, el_writer{nullptr, nullptr}, line, sizeof(line)));
    EXPECT_VIOLATION(el_log_writer_sink_init(&writer_sink, out, nullptr, 1));
    EXPECT_VIOLATION((void)el_log_writer_sink_as_sink(nullptr));
    EXPECT_VIOLATION((void)el_log_writer_sink_lost(nullptr));
}

} // namespace
