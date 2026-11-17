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
        el_ring_init(&ring, ring_storage, sizeof(ring_storage));
        el_log_deferred_sink_init(&deferred, &ring, record_storage, sizeof(record_storage));
        recording_sink_init(&recording);
    }

    uint8_t ring_storage[128];
    uint8_t record_storage[64];
    el_ring ring;
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

    /* No record storage, and storage that is not used. */
    el_log_deferred_sink_init(&deferred, &ring, nullptr, 0);
    el_log_deferred_sink_init(&deferred, &ring, record_storage, 0);
    EXPECT_EQ(el_log_sink_write(el_log_deferred_sink_as_sink(&deferred), &record), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_deferred_sink_drain(&deferred, to), 0u);

    el_log_deferred_sink_init(&deferred, &ring, record_storage, sizeof(record_storage));
    EXPECT_EQ(el_log_sink_write(el_log_deferred_sink_as_sink(&deferred), &record), EL_OK);
    EXPECT_EQ(el_log_deferred_sink_drain(&deferred, to), 1u);
    EXPECT_EQ(el_log_deferred_sink_lost(&deferred), 0u);
    (void)el_log_deferred_sink_record_size(0);
}

TEST_F(LogDeferredViolationsDeathTest, CallerSetsADeferredSinkUpWithoutASinkARingOrStorage)
{
    EXPECT_VIOLATION(
        el_log_deferred_sink_init(nullptr, &ring, record_storage, sizeof(record_storage)));
    EXPECT_VIOLATION(
        el_log_deferred_sink_init(&deferred, nullptr, record_storage, sizeof(record_storage)));
    EXPECT_VIOLATION(el_log_deferred_sink_init(&deferred, &ring, nullptr, 1));
}

TEST_F(LogDeferredViolationsDeathTest, CallerGivesADeferredSinkFunctionNoSink)
{
    EXPECT_VIOLATION((void)el_log_deferred_sink_as_sink(nullptr));
    EXPECT_VIOLATION((void)el_log_deferred_sink_lost(nullptr));
    EXPECT_VIOLATION((void)el_log_deferred_sink_drain(nullptr, recording_sink_as_sink(&recording)));
}

/* Caught with nothing to drain too: the handle is checked before the ring
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

} // namespace
