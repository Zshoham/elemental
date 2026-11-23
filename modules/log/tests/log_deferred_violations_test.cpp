/*
 * Contract violations of the deferred sink are caught
 * (elemental/log_deferred.h).
 *
 * Death tests: each violation runs in a child process, and the test passes
 * if that process is stopped by the check, in whichever way the assertion
 * configuration stops it.
 */
#include <gtest/gtest.h>

#include <elemental/log.h>
#include <elemental/log_deferred.h>
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

/* Stopped by the check, not by a crash: see log_violations_test.cpp. */
bool StoppedByACheck(int status)
{
    return WIFSIGNALED(status) && (WTERMSIG(status) == SIGILL || WTERMSIG(status) == SIGTRAP ||
                                   WTERMSIG(status) == SIGABRT);
}

#define EXPECT_VIOLATION(statement) EXPECT_EXIT(statement, StoppedByACheck, "")

class LogDeferredViolations : public testing::Test
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
        el_log_deferred_sink_init(&deferred, storage, sizeof(storage), kMessageMax);
        recording_sink_init(&recording);
    }

    static constexpr size_t kMessageMax = 32;
    /* Three records of the longest message wait, and one is built. */
    uint8_t storage[EL_LOG_DEFERRED_SINK_STORAGE_SIZE(3, kMessageMax)];
    el_log_deferred_sink deferred;
    recording_sink recording;
};

/* GoogleTest runs suites named *DeathTest first, before any threads exist. */
using LogDeferredViolationsDeathTest = LogDeferredViolations;

TEST_F(LogDeferredViolations, CorrectUseDoesNotTripACheck)
{
    const el_log_sink to = recording_sink_as_sink(&recording);
    el_log_record record;
    record.timestamp = 0;
    record.ticks_per_second = 1;
    record.level = EL_LOG_LEVEL_INFO;
    record.tag = "tag";
    record.message = nullptr;
    record.len = 0;

    /* No storage at all, which loses every record, whatever the longest
     * message: no record area is sized for it; and storage that is not
     * used. */
    el_log_deferred_sink_init(&deferred, nullptr, 0, SIZE_MAX);
    el_log_deferred_sink_init(&deferred, nullptr, 0, kMessageMax);
    el_log_deferred_sink_init(&deferred, storage, 0, kMessageMax);
    EXPECT_EQ(el_log_sink_write(el_log_deferred_sink_as_sink(&deferred), &record), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_deferred_sink_drain(&deferred, to), 0u);
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&deferred), 0u);

    /* Exactly one record's worth: a sink that builds a record and keeps none. */
    el_log_deferred_sink_init(&deferred, storage, el_log_deferred_sink_record_size(kMessageMax),
                              kMessageMax);
    EXPECT_EQ(el_log_sink_write(el_log_deferred_sink_as_sink(&deferred), &record), EL_ERR_NO_SPACE);

    el_log_deferred_sink_init(&deferred, storage, sizeof(storage), kMessageMax);
    EXPECT_EQ(el_log_sink_write(el_log_deferred_sink_as_sink(&deferred), &record), EL_OK);
    EXPECT_EQ(el_log_deferred_sink_drain(&deferred, to), 1u);
    EXPECT_EQ(el_log_deferred_sink_lost(&deferred), 0u);
    (void)el_log_deferred_sink_record_size(0);
}

TEST_F(LogDeferredViolationsDeathTest, CallerSetsADeferredSinkUpWithoutASinkOrWithoutStorage)
{
    EXPECT_VIOLATION(el_log_deferred_sink_init(nullptr, storage, sizeof(storage), kMessageMax));
    EXPECT_VIOLATION(el_log_deferred_sink_init(&deferred, nullptr, 1, 0));
    EXPECT_VIOLATION(el_log_deferred_sink_init(&deferred, nullptr, sizeof(storage), kMessageMax));
}

TEST_F(LogDeferredViolationsDeathTest, CallerGivesADeferredSinkLessStorageThanOneRecord)
{
    const size_t one = el_log_deferred_sink_record_size(kMessageMax);
    EXPECT_VIOLATION(el_log_deferred_sink_init(&deferred, storage, 1, kMessageMax));
    EXPECT_VIOLATION(el_log_deferred_sink_init(&deferred, storage, one - 1, kMessageMax));
}

/* The two sizes exchanged: the "longest message" is far more than the
 * storage holds, so a valid call is never mistaken for this one. */
TEST_F(LogDeferredViolationsDeathTest, CallerSwapsTheStorageSizeAndTheLongestMessage)
{
    EXPECT_VIOLATION(el_log_deferred_sink_init(&deferred, storage, kMessageMax, sizeof(storage)));
}

TEST_F(LogDeferredViolationsDeathTest, CallerGivesADeferredSinkFunctionNoSink)
{
    EXPECT_VIOLATION((void)el_log_deferred_sink_as_sink(nullptr));
    EXPECT_VIOLATION((void)el_log_deferred_sink_lost(nullptr));
    EXPECT_VIOLATION((void)el_log_deferred_sink_pending_bytes(nullptr));
    EXPECT_VIOLATION((void)el_log_deferred_sink_drain(nullptr, recording_sink_as_sink(&recording)));
}

/* Caught with nothing to drain too: the handle is checked before the queue
 * is looked at. */
TEST_F(LogDeferredViolationsDeathTest, CallerDrainsIntoASinkHandleThatWasNeverMade)
{
    EXPECT_VIOLATION((void)el_log_deferred_sink_drain(&deferred, el_log_sink{nullptr, nullptr}));
    EXPECT_VIOLATION(
        (void)el_log_deferred_sink_drain(&deferred, recording_sink_without_begin(&recording)));
    EXPECT_VIOLATION(
        (void)el_log_deferred_sink_drain(&deferred, recording_sink_without_end(&recording)));
}

TEST_F(LogDeferredViolationsDeathTest, CallerAsksForTheSizeOfARecordThatCannotExist)
{
    EXPECT_VIOLATION((void)el_log_deferred_sink_record_size(SIZE_MAX));
}

/* A compose function that logs through the logger it is given. */
el_status ComposeAndLogAgain(void *ctx, el_buf *message)
{
    el_log_write(static_cast<const el_logger *>(ctx), EL_LOG_LEVEL_INFO, "again", 5);
    return el_buf_append(message, "outer", 5);
}

TEST_F(LogDeferredViolationsDeathTest, ComposeFunctionLogsToTheDeferredSinkItIsWritingInto)
{
    el_manual_clock manual;
    el_manual_clock_init(&manual, 1000);
    el_logger log;
    el_logger_init(&log, el_log_deferred_sink_as_sink(&deferred), el_manual_clock_as_clock(&manual),
                   "tag", EL_LOG_LEVEL_DEBUG);
    EXPECT_VIOLATION(el_log_compose(&log, EL_LOG_LEVEL_INFO, ComposeAndLogAgain, &log));
}
} // namespace
