/*
 * The compile-time cut-off, set for this one file
 * (docs/adr/0021-log-cutoff.md): calls below it are not in the program, so
 * they neither log nor evaluate their arguments, whatever the logger's own
 * level. That the call and its format string are absent from the object is
 * checked on the object itself (log.cutoff_check.*, host).
 */
#define EL_LOG_CUTOFF EL_LOG_LEVEL_WARN

#include <gtest/gtest.h>

#include <elemental/log.h>
#include <elemental/log_printf.h>

#include <string>

#include "log_support.h"

namespace
{

class ElLogCutoff : public testing::Test
{
  protected:
    void SetUp() override
    {
        recording_sink_init(&sink);
        counting_clock_init(&clock, 1000);
        /* The logger itself lets everything through. */
        el_logger_init(&log, recording_sink_as_sink(&sink), counting_clock_as_clock(&clock), "cut",
                       EL_LOG_LEVEL_DEBUG);
    }

    recording_sink sink;
    counting_clock clock;
    el_logger log;
    int evaluated = 0;
};

} // namespace

TEST_F(ElLogCutoff, ACallBelowItDoesNotLogAndDoesNotEvaluateItsArguments)
{
    EL_LOG_DEBUG(&log, "%d", ++evaluated);
    EL_LOG_INFO(&log, "%d", ++evaluated);

    EXPECT_EQ(sink.calls, 0u);
    EXPECT_EQ(evaluated, 0);
    EXPECT_EQ(clock.now_calls, 0u);
}

TEST_F(ElLogCutoff, ACallAtOrAboveItLogsAsUsual)
{
    EL_LOG_WARN(&log, "%d", ++evaluated);
    EXPECT_EQ(std::string(sink.message, sink.last.len), "1");
    EL_LOG_ERROR(&log, "%d", ++evaluated);
    EXPECT_EQ(std::string(sink.message, sink.last.len), "2");

    EXPECT_EQ(sink.calls, 2u);
    EXPECT_EQ(evaluated, 2);
}

TEST_F(ElLogCutoff, TheLoggersLevelStillAppliesToWhatIsKept)
{
    el_logger_set_level(&log, EL_LOG_LEVEL_ERROR);
    EL_LOG_WARN(&log, "kept by the cut-off, stopped by the logger");

    EXPECT_EQ(sink.calls, 0u);
}

TEST_F(ElLogCutoff, TheFunctionsAreNotAffected)
{
    /* Only the macros honor the cut-off. */
    el_log_printf(&log, EL_LOG_LEVEL_DEBUG, "%d", 5);
    el_log_write(&log, EL_LOG_LEVEL_INFO, "x", 1);

    EXPECT_EQ(sink.calls, 2u);
}

namespace
{

el_status CountedCompose(void *ctx, el_buf *message)
{
    ++*static_cast<int *>(ctx);
    return el_buf_append(message, "composed", 8);
}

} // namespace

/* A constant, so that it can guard a declaration or a static array too. */
static_assert(!EL_LOG_KEPT(EL_LOG_LEVEL_DEBUG), "below the cut-off");
static_assert(!EL_LOG_KEPT(EL_LOG_LEVEL_INFO), "below the cut-off");
static_assert(EL_LOG_KEPT(EL_LOG_LEVEL_WARN), "at the cut-off");
static_assert(EL_LOG_KEPT(EL_LOG_LEVEL_ERROR), "above the cut-off");

TEST_F(ElLogCutoff, AComposedMessageUnderKeptIsCutOffWithTheCallsAndNotComposed)
{
    int composed = 0;
    if (EL_LOG_KEPT(EL_LOG_LEVEL_INFO)) {
        el_log_compose(&log, EL_LOG_LEVEL_INFO, CountedCompose, &composed);
    }
    EXPECT_EQ(composed, 0);
    EXPECT_EQ(sink.calls, 0u);
    EXPECT_EQ(clock.now_calls, 0u);

    if (EL_LOG_KEPT(EL_LOG_LEVEL_WARN)) {
        el_log_compose(&log, EL_LOG_LEVEL_WARN, CountedCompose, &composed);
    }
    EXPECT_EQ(composed, 1);
    EXPECT_EQ(sink.calls, 1u);
    EXPECT_EQ(std::string(sink.message, sink.last.len), "composed");
}

TEST_F(ElLogCutoff, ComposeItselfIsNotAffected)
{
    /* Only the macro and the guard honor the cut-off. */
    int composed = 0;
    el_log_compose(&log, EL_LOG_LEVEL_DEBUG, CountedCompose, &composed);

    EXPECT_EQ(composed, 1);
    EXPECT_EQ(sink.calls, 1u);
}
