/*
 * Contract violations are caught, on both sides of the interface: callers
 * that break a precondition, and clocks that break the operation contract.
 *
 * Death tests: each violation runs in a child process, and the test passes
 * if that process is stopped by the check, in whichever way the assertion
 * configuration stops it.
 */
#include <gtest/gtest.h>

#include <elemental/time.h>
#include <elemental/time_manual.h>

#include <cstdint>

#include "scripted_clock.h"

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

class TimeViolations : public testing::Test
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
        scripted = {};
        scripted.ticks_per_second = 1000;
        el_manual_clock_init(&manual, 1000);
    }

    scripted_clock scripted;
    el_manual_clock manual;
};

/* GoogleTest runs suites named *DeathTest first, before any threads exist. */
using TimeViolationsDeathTest = TimeViolations;

TEST_F(TimeViolations, CorrectUseDoesNotTripACheck)
{
    /* Including the edge cases. A check that fired would end the test. */
    uint32_t us;
    EXPECT_EQ(el_ticks_from_ms(0, 1), 0u);
    EXPECT_EQ(el_ticks_to_seconds(0, 1, &us), 0u);
    EXPECT_EQ(el_ticks_to_seconds(UINT64_MAX, UINT32_MAX, &us), UINT64_MAX / UINT32_MAX);
    EXPECT_EQ(el_clock_ticks_from_ms(scripted_clock_as_clock(&scripted), 1), 1u);
    EXPECT_EQ(el_clock_ticks_to_seconds(scripted_clock_as_clock(&scripted), 1000, &us), 1u);
    scripted.ticks_per_second = 1;
    EXPECT_EQ(el_clock_ticks_per_second(scripted_clock_as_clock(&scripted)), 1u);
    el_manual_clock_init(&manual, 1);
    el_manual_clock_advance(&manual, UINT64_MAX);
    el_manual_clock_advance(&manual, 0);
    EXPECT_EQ(el_clock_now(el_manual_clock_as_clock(&manual)), UINT64_MAX);
}

TEST_F(TimeViolationsDeathTest, CallerUsesAHandleThatWasNeverMade)
{
    EXPECT_VIOLATION((void)el_clock_now(el_clock{nullptr, nullptr}));
    EXPECT_VIOLATION((void)el_clock_ticks_per_second(el_clock{nullptr, nullptr}));
    EXPECT_VIOLATION((void)el_clock_now(scripted_clock_without_now(&scripted)));
    EXPECT_VIOLATION(
        (void)el_clock_ticks_per_second(scripted_clock_without_ticks_per_second(&scripted)));
    uint32_t us;
    EXPECT_VIOLATION((void)el_clock_ticks_from_ms(el_clock{nullptr, nullptr}, 1));
    EXPECT_VIOLATION((void)el_clock_ticks_to_seconds(el_clock{nullptr, nullptr}, 1, &us));
}

TEST_F(TimeViolationsDeathTest, ClockStatesAFrequencyOfZero)
{
    scripted.ticks_per_second = 0;
    EXPECT_VIOLATION((void)el_clock_ticks_per_second(scripted_clock_as_clock(&scripted)));
}

TEST_F(TimeViolationsDeathTest, CallerConvertsWithAFrequencyOfZero)
{
    uint32_t us;
    EXPECT_VIOLATION((void)el_ticks_from_ms(1, 0));
    EXPECT_VIOLATION((void)el_ticks_to_seconds(1, 0, &us));
}

TEST_F(TimeViolationsDeathTest, CallerPassesANullOutputParameter)
{
    EXPECT_VIOLATION((void)el_ticks_to_seconds(1, 1000, nullptr));
    EXPECT_VIOLATION(
        (void)el_clock_ticks_to_seconds(scripted_clock_as_clock(&scripted), 1, nullptr));
}

TEST_F(TimeViolationsDeathTest, CallerPassesANullManualClock)
{
    EXPECT_VIOLATION(el_manual_clock_init(nullptr, 1000));
    EXPECT_VIOLATION(el_manual_clock_advance(nullptr, 1));
    EXPECT_VIOLATION((void)el_manual_clock_as_clock(nullptr));
}

TEST_F(TimeViolationsDeathTest, CallerGivesAManualClockAFrequencyOfZero)
{
    EXPECT_VIOLATION(el_manual_clock_init(&manual, 0));
}

TEST_F(TimeViolationsDeathTest, CallerAdvancesAManualClockPastTheLargestReading)
{
    el_manual_clock_advance(&manual, UINT64_MAX - 1u);
    EXPECT_VIOLATION(el_manual_clock_advance(&manual, 2));
}

} // namespace
