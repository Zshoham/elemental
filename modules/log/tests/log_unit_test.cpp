/*
 * The logger, el_log_sink_write(), and the printf call site: what
 * elemental/log.h and elemental/log_printf.h promise about when a record is
 * made, what it holds, and when the clock is read.
 */
#include <gtest/gtest.h>

#include <elemental/log.h>
#include <elemental/log_printf.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>

#include "log_support.h"

namespace
{

class ElLogger : public testing::Test
{
  protected:
    void SetUp() override
    {
        recording_sink_init(&sink);
        counting_clock_init(&clock, 1000);
        el_logger_init(&log, recording_sink_as_sink(&sink), counting_clock_as_clock(&clock), "net",
                       EL_LOG_LEVEL_INFO);
    }

    /* The message of the last record the sink was given. */
    std::string LastMessage() const
    {
        return std::string(sink.message, sink.last.len);
    }

    recording_sink sink;
    counting_clock clock;
    el_logger log;
};

using ElLogPrintf = ElLogger;

/* What a wrapper around el_log_vprintf() looks like. Declared with the
 * same format check, so that its callers' arguments are checked too. */
void LogThroughAVaList(const el_logger *log, el_log_level level, const char *format, ...)
    EL_FMT_PRINTF_CHECK(3, 4);

void LogThroughAVaList(const el_logger *log, el_log_level level, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    el_log_vprintf(log, level, format, args);
    va_end(args);
}

} // namespace

/* ---- Levels -------------------------------------------------------------- */

TEST(ElLogLevel, NamesAreStableAndLowerCase)
{
    EXPECT_STREQ(el_log_level_name(EL_LOG_LEVEL_DEBUG), "debug");
    EXPECT_STREQ(el_log_level_name(EL_LOG_LEVEL_INFO), "info");
    EXPECT_STREQ(el_log_level_name(EL_LOG_LEVEL_WARN), "warn");
    EXPECT_STREQ(el_log_level_name(EL_LOG_LEVEL_ERROR), "error");
    EXPECT_STREQ(el_log_level_name(EL_LOG_LEVEL_OFF), "off");
    EXPECT_STREQ(el_log_level_name(5), "unknown");
    EXPECT_STREQ(el_log_level_name(255), "unknown");
}

TEST(ElLogLevel, AreOrderedByHowMuchARecordMatters)
{
    EXPECT_LT(EL_LOG_LEVEL_DEBUG, EL_LOG_LEVEL_INFO);
    EXPECT_LT(EL_LOG_LEVEL_INFO, EL_LOG_LEVEL_WARN);
    EXPECT_LT(EL_LOG_LEVEL_WARN, EL_LOG_LEVEL_ERROR);
    EXPECT_LT(EL_LOG_LEVEL_ERROR, EL_LOG_LEVEL_OFF);
}

/* ---- The logger ---------------------------------------------------------- */

TEST_F(ElLogger, ARecordCarriesTheLevelTheTagTheTimeAndTheMessage)
{
    clock.reading = 12034;
    el_log_write(&log, EL_LOG_LEVEL_WARN, "retrying", 8);

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(sink.last.level, EL_LOG_LEVEL_WARN);
    EXPECT_STREQ(sink.last.tag, "net");
    EXPECT_EQ(sink.last.timestamp, 12034u);
    EXPECT_EQ(sink.last.ticks_per_second, 1000u);
    EXPECT_EQ(LastMessage(), "retrying");
    EXPECT_EQ(sink.last_ctx, &sink);
}

TEST_F(ElLogger, TheTagIsPassedByPointerAndTheMessageAsGiven)
{
    static const char tag[] = "storage";
    el_logger_init(&log, recording_sink_as_sink(&sink), counting_clock_as_clock(&clock), tag,
                   EL_LOG_LEVEL_DEBUG);
    const char message[] = "only the first four";
    el_log_write(&log, EL_LOG_LEVEL_DEBUG, message, 4);

    EXPECT_EQ(sink.last.tag, tag);
    EXPECT_EQ(sink.last_message_pointer, message);
    /* A length, not a terminator, says where the message ends. */
    EXPECT_EQ(LastMessage(), "only");
}

TEST_F(ElLogger, BelowItsMinimumNothingHappens)
{
    el_log_write(&log, EL_LOG_LEVEL_DEBUG, "noise", 5);

    EXPECT_EQ(sink.calls, 0u);
    EXPECT_EQ(clock.now_calls, 0u) << "the clock is read only for a record that is made";
}

TEST_F(ElLogger, EveryLevelFromItsMinimumUpIsLetThrough)
{
    for (el_log_level minimum = EL_LOG_LEVEL_DEBUG; minimum <= EL_LOG_LEVEL_OFF; minimum++) {
        el_logger_set_level(&log, minimum);
        for (el_log_level level = EL_LOG_LEVEL_DEBUG; level < EL_LOG_LEVEL_OFF; level++) {
            const unsigned before = sink.calls;
            el_log_write(&log, level, "x", 1);
            EXPECT_EQ(sink.calls - before, level >= minimum ? 1u : 0u)
                << el_log_level_name(level) << " through a minimum of "
                << el_log_level_name(minimum);
        }
    }
}

TEST_F(ElLogger, AMinimumOfOffMakesNoRecords)
{
    el_logger_init(&log, recording_sink_as_sink(&sink), counting_clock_as_clock(&clock), "",
                   EL_LOG_LEVEL_OFF);
    el_log_write(&log, EL_LOG_LEVEL_ERROR, "fire", 4);

    EXPECT_EQ(sink.calls, 0u);
    EXPECT_EQ(clock.now_calls, 0u);
}

TEST_F(ElLogger, TheClockIsReadOnceForEachRecordMade)
{
    for (unsigned i = 1; i <= 5; i++) {
        clock.reading = i * 100u;
        el_log_write(&log, EL_LOG_LEVEL_ERROR, "x", 1);
        EXPECT_EQ(clock.now_calls, i);
        EXPECT_EQ(sink.last.timestamp, i * 100u);
    }
}

TEST_F(ElLogger, TheClocksFrequencyIsReadOnceWhenTheLoggerIsSetUp)
{
    EXPECT_EQ(clock.ticks_per_second_calls, 1u);
    EXPECT_EQ(clock.now_calls, 0u) << "setting a logger up reads no time";

    for (int i = 0; i < 3; i++) {
        el_log_write(&log, EL_LOG_LEVEL_ERROR, "x", 1);
    }
    EXPECT_EQ(clock.ticks_per_second_calls, 1u);
    EXPECT_EQ(sink.last.ticks_per_second, 1000u);
}

TEST_F(ElLogger, AnEmptyMessageIsARecord)
{
    el_log_write(&log, EL_LOG_LEVEL_INFO, nullptr, 0);
    el_log_write(&log, EL_LOG_LEVEL_INFO, "", 0);

    EXPECT_EQ(sink.calls, 2u);
    EXPECT_EQ(sink.last.len, 0u);
}

TEST_F(ElLogger, AnEmptyTagIsAllowed)
{
    el_logger_init(&log, recording_sink_as_sink(&sink), counting_clock_as_clock(&clock), "",
                   EL_LOG_LEVEL_DEBUG);
    el_log_write(&log, EL_LOG_LEVEL_DEBUG, "x", 1);

    EXPECT_STREQ(sink.last.tag, "");
}

TEST_F(ElLogger, ARecordTheSinkRefusesIsLostAndTheNextOneStillGoes)
{
    sink.answer = EL_ERR_IO;
    el_log_write(&log, EL_LOG_LEVEL_ERROR, "lost", 4);
    EXPECT_EQ(sink.calls, 1u);

    sink.answer = EL_OK;
    el_log_write(&log, EL_LOG_LEVEL_ERROR, "kept", 4);
    EXPECT_EQ(sink.calls, 2u);
    EXPECT_EQ(LastMessage(), "kept");
}

TEST_F(ElLogger, TwoLoggersShareASinkAndKeepTheirOwnTagAndLevel)
{
    el_logger quiet;
    el_logger_init(&quiet, recording_sink_as_sink(&sink), counting_clock_as_clock(&clock), "disk",
                   EL_LOG_LEVEL_ERROR);

    el_log_write(&quiet, EL_LOG_LEVEL_WARN, "x", 1);
    EXPECT_EQ(sink.calls, 0u);
    el_log_write(&log, EL_LOG_LEVEL_WARN, "x", 1);
    EXPECT_STREQ(sink.last.tag, "net");
    el_log_write(&quiet, EL_LOG_LEVEL_ERROR, "x", 1);
    EXPECT_STREQ(sink.last.tag, "disk");
    EXPECT_EQ(sink.calls, 2u);
}

TEST_F(ElLogger, ACopyOfALoggerLogsAsTheOriginal)
{
    const el_logger copy = log;
    clock.reading = 7;
    el_log_write(&copy, EL_LOG_LEVEL_WARN, "x", 1);

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_STREQ(sink.last.tag, "net");
    EXPECT_EQ(sink.last.timestamp, 7u);
}

/* ---- Driving a sink directly ---------------------------------------------- */

TEST(ElLogSinkWrite, PassesTheRecordOnAndReturnsTheSinksStatus)
{
    recording_sink sink;
    recording_sink_init(&sink);
    el_log_record record;
    record.timestamp = UINT64_MAX;
    record.ticks_per_second = UINT32_MAX;
    record.level = EL_LOG_LEVEL_ERROR;
    record.tag = "tag";
    record.message = "bytes";
    record.len = 5;

    EXPECT_EQ(el_log_sink_write(recording_sink_as_sink(&sink), &record), EL_OK);
    EXPECT_EQ(sink.calls, 1u);
    EXPECT_EQ(sink.last.timestamp, UINT64_MAX);
    EXPECT_EQ(sink.last.ticks_per_second, UINT32_MAX);
    EXPECT_EQ(sink.last.level, EL_LOG_LEVEL_ERROR);
    EXPECT_STREQ(sink.last.tag, "tag");
    EXPECT_EQ(std::string(sink.message, sink.last.len), "bytes");

    /* Elemental's codes and an application's own come back unchanged. */
    const el_status statuses[] = {EL_ERR_WOULD_BLOCK, EL_ERR_IO, EL_ERR_NO_SPACE, -7};
    for (const el_status status : statuses) {
        sink.answer = status;
        EXPECT_EQ(el_log_sink_write(recording_sink_as_sink(&sink), &record), status);
    }
}

/* ---- The printf call site -------------------------------------------------- */

TEST_F(ElLogPrintf, FormatsTheMessageAndLogsIt)
{
    clock.reading = 42;
    el_log_printf(&log, EL_LOG_LEVEL_WARN, "%s did not answer, attempt %d of %d", "10.0.0.7", 2, 3);

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(LastMessage(), "10.0.0.7 did not answer, attempt 2 of 3");
    EXPECT_EQ(sink.last.level, EL_LOG_LEVEL_WARN);
    EXPECT_STREQ(sink.last.tag, "net");
    EXPECT_EQ(sink.last.timestamp, 42u);
}

TEST_F(ElLogPrintf, TheMacrosLogAtTheirLevels)
{
    el_logger_set_level(&log, EL_LOG_LEVEL_DEBUG);

    EL_LOG_DEBUG(&log, "d%d", 1);
    EXPECT_EQ(sink.last.level, EL_LOG_LEVEL_DEBUG);
    EXPECT_EQ(LastMessage(), "d1");
    EL_LOG_INFO(&log, "i%d", 2);
    EXPECT_EQ(sink.last.level, EL_LOG_LEVEL_INFO);
    EXPECT_EQ(LastMessage(), "i2");
    EL_LOG_WARN(&log, "w%d", 3);
    EXPECT_EQ(sink.last.level, EL_LOG_LEVEL_WARN);
    EXPECT_EQ(LastMessage(), "w3");
    EL_LOG_ERROR(&log, "e%d", 4);
    EXPECT_EQ(sink.last.level, EL_LOG_LEVEL_ERROR);
    EXPECT_EQ(LastMessage(), "e4");
    EL_LOG(&log, EL_LOG_LEVEL_WARN, "plain text, no arguments");
    EXPECT_EQ(LastMessage(), "plain text, no arguments");
    EXPECT_EQ(sink.calls, 5u);
}

TEST_F(ElLogPrintf, AMacroIsOneStatement)
{
    el_logger_set_level(&log, EL_LOG_LEVEL_DEBUG);
    const bool yes = true;

    /* No braces: a macro that was two statements would log either way, or
     * break the else. */
    if (!yes)
        EL_LOG_ERROR(&log, "not this");
    else
        EL_LOG_INFO(&log, "this");

    EXPECT_EQ(sink.calls, 1u);
    EXPECT_EQ(LastMessage(), "this");
}

TEST_F(ElLogPrintf, BelowTheLoggersMinimumNothingIsFormatted)
{
    /* A null %s is a contract violation if printf ever reads it. Volatile,
     * so that the compiler cannot see the null and object at compile time. */
    const char *volatile never_read = nullptr;
    el_log_printf(&log, EL_LOG_LEVEL_DEBUG, "%s", never_read);
    EL_LOG_DEBUG(&log, "%s", never_read);

    EXPECT_EQ(sink.calls, 0u);
    EXPECT_EQ(clock.now_calls, 0u);
}

TEST_F(ElLogPrintf, AMessageLongerThanTheLimitIsCutAndStillLogged)
{
    const std::string filler(EL_TEST_LOG_MESSAGE_MAX + 100, 'x');
    el_log_printf(&log, EL_LOG_LEVEL_ERROR, "%s", filler.c_str());

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(sink.last.len, static_cast<size_t>(EL_TEST_LOG_MESSAGE_MAX));
    EXPECT_EQ(LastMessage(), filler.substr(0, EL_TEST_LOG_MESSAGE_MAX));
}

TEST_F(ElLogPrintf, AMessageOfExactlyTheLimitIsWhole)
{
    const std::string filler(EL_TEST_LOG_MESSAGE_MAX, 'y');
    el_log_printf(&log, EL_LOG_LEVEL_ERROR, "%s", filler.c_str());

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(LastMessage(), filler);
}

TEST_F(ElLogPrintf, ANumberThatDoesNotFitIsLeftOutNotCut)
{
    /* Two bytes of room for a five-digit number: by printf's item rule the
     * number is absent, and nothing follows it. */
    const std::string filler(EL_TEST_LOG_MESSAGE_MAX - 2, 'z');
    el_log_printf(&log, EL_LOG_LEVEL_ERROR, "%s%d!", filler.c_str(), 12345);

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(LastMessage(), filler);
}

TEST_F(ElLogPrintf, AVaListWrapperLogsTheSame)
{
    LogThroughAVaList(&log, EL_LOG_LEVEL_INFO, "%u of %u", 3u, 4u);
    EXPECT_EQ(LastMessage(), "3 of 4");

    LogThroughAVaList(&log, EL_LOG_LEVEL_DEBUG, "%u", 1u);
    EXPECT_EQ(sink.calls, 1u);
}

TEST_F(ElLogPrintf, AnEmptyFormatIsAnEmptyRecord)
{
    el_log_printf(&log, EL_LOG_LEVEL_ERROR, "%s", "");

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(sink.last.len, 0u);
}
