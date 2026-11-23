/*
 * The logger, el_log_sink_write(), and the printf call site: what
 * elemental/log.h and elemental/log_printf.h promise about when a record is
 * made, what it holds, when the clock is read, and how its message reaches
 * the buffer the sink lends.
 */
#include <gtest/gtest.h>

#include <elemental/io_buf.h>
#include <elemental/log.h>
#include <elemental/log_printf.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

TEST_F(ElLogger, TheTagIsPassedByPointerAndTheMessageIsCopiedIntoTheSinksBuffer)
{
    static const char tag[] = "storage";
    el_logger_init(&log, recording_sink_as_sink(&sink), counting_clock_as_clock(&clock), tag,
                   EL_LOG_LEVEL_DEBUG);
    const char message[] = "only the first four";
    el_log_write(&log, EL_LOG_LEVEL_DEBUG, message, 4);

    EXPECT_EQ(sink.last.tag, tag);
    EXPECT_EQ(sink.last_message_pointer, sink.storage);
    /* A length, not a terminator, says where the message ends. */
    EXPECT_EQ(LastMessage(), "only");
    EXPECT_EQ(sink.last_fit, EL_OK);
}

TEST_F(ElLogger, ARecordIsBegunWithoutItsMessageAndEndedWithIt)
{
    clock.reading = 77;
    el_log_write(&log, EL_LOG_LEVEL_WARN, "late", 4);

    ASSERT_EQ(sink.begins, 1u);
    EXPECT_EQ(sink.begun.message, nullptr);
    EXPECT_EQ(sink.begun.len, 0u);
    EXPECT_EQ(sink.begun.timestamp, 77u);
    EXPECT_EQ(sink.begun.ticks_per_second, 1000u);
    EXPECT_EQ(sink.begun.level, EL_LOG_LEVEL_WARN);
    EXPECT_STREQ(sink.begun.tag, "net");

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(sink.last.timestamp, 77u);
    EXPECT_EQ(sink.last.level, EL_LOG_LEVEL_WARN);
    EXPECT_EQ(LastMessage(), "late");
}

TEST_F(ElLogger, AMessageLongerThanTheSinksBufferIsCutAtAByteAndTheSinkIsTold)
{
    recording_sink_set_capacity(&sink, 5);
    el_log_write(&log, EL_LOG_LEVEL_ERROR, "0123456789", 10);

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(LastMessage(), "01234");
    EXPECT_EQ(sink.last_fit, EL_ERR_NO_SPACE);
}

TEST_F(ElLogger, AMessageThatFillsTheSinksBufferExactlyIsWhole)
{
    recording_sink_set_capacity(&sink, 5);
    el_log_write(&log, EL_LOG_LEVEL_ERROR, "01234", 5);

    EXPECT_EQ(LastMessage(), "01234");
    EXPECT_EQ(sink.last_fit, EL_OK);
}

TEST_F(ElLogger, ARecordTheSinkDoesNotBeginIsLostAndNotEnded)
{
    sink.begin_answer = EL_ERR_IO;
    el_log_write(&log, EL_LOG_LEVEL_ERROR, "lost", 4);
    EXPECT_EQ(sink.begins, 1u);
    EXPECT_EQ(sink.calls, 0u);

    sink.begin_answer = EL_OK;
    el_log_write(&log, EL_LOG_LEVEL_ERROR, "kept", 4);
    EXPECT_EQ(sink.calls, 1u);
    EXPECT_EQ(LastMessage(), "kept");
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

    /* The record is begun without its message, which goes into the
     * sink's buffer. */
    EXPECT_EQ(sink.begins, 1u);
    EXPECT_EQ(sink.begun.message, nullptr);
    EXPECT_EQ(sink.begun.len, 0u);
    EXPECT_EQ(sink.begun.timestamp, UINT64_MAX);
    EXPECT_EQ(sink.last_message_pointer, sink.storage);
    EXPECT_EQ(sink.last_fit, EL_OK);

    /* Elemental's codes and an application's own come back unchanged, from
     * either operation. */
    const el_status statuses[] = {EL_ERR_WOULD_BLOCK, EL_ERR_IO, EL_ERR_NO_SPACE, -7};
    for (const el_status status : statuses) {
        sink.answer = status;
        EXPECT_EQ(el_log_sink_write(recording_sink_as_sink(&sink), &record), status);
        sink.answer = EL_OK;
        sink.begin_answer = status;
        const unsigned ended = sink.calls;
        EXPECT_EQ(el_log_sink_write(recording_sink_as_sink(&sink), &record), status);
        EXPECT_EQ(sink.calls, ended) << "a record that was not begun is not ended";
        sink.begin_answer = EL_OK;
    }
}

TEST(ElLogSinkWrite, AMessageTheBufferCannotTakeIsCutAndTheSinkIsTold)
{
    recording_sink sink;
    recording_sink_init(&sink);
    recording_sink_set_capacity(&sink, 3);
    el_log_record record;
    record.timestamp = 0;
    record.ticks_per_second = 1;
    record.level = EL_LOG_LEVEL_INFO;
    record.tag = "";
    record.message = "bytes";
    record.len = 5;

    EXPECT_EQ(el_log_sink_write(recording_sink_as_sink(&sink), &record), EL_OK)
        << "the sink's own answer";
    EXPECT_EQ(std::string(sink.message, sink.last.len), "byt");
    EXPECT_EQ(sink.last_fit, EL_ERR_NO_SPACE);
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

TEST_F(ElLogPrintf, IsFormattedStraightIntoTheSinksBuffer)
{
    el_log_printf(&log, EL_LOG_LEVEL_ERROR, "%d-%d", 1, 2);

    EXPECT_EQ(sink.last_message_pointer, sink.storage) << "no copy on the way";
    EXPECT_EQ(LastMessage(), "1-2");
}

TEST_F(ElLogPrintf, TheMessageFollowsWhatTheSinkAlreadyHasInItsBuffer)
{
    /* A sink that starts each record with bytes of its own, as the writer
     * sink starts a line. */
    char storage[32];
    el_buf lent;
    el_buf_init(&lent, storage, sizeof(storage));
    ASSERT_EQ(el_buf_append(&lent, "pre:", 4), EL_OK);
    sink.lend = &lent;

    el_log_printf(&log, EL_LOG_LEVEL_ERROR, "%s", "msg");

    EXPECT_EQ(sink.last_message_pointer, storage + 4);
    EXPECT_EQ(LastMessage(), "msg");
    EXPECT_EQ(std::string(storage, el_buf_len(&lent)), "pre:msg");
}

TEST_F(ElLogPrintf, AMessageLongerThanTheSinksBufferIsCutAndTheSinkIsTold)
{
    recording_sink_set_capacity(&sink, 16);
    const std::string filler(100, 'x');
    el_log_printf(&log, EL_LOG_LEVEL_ERROR, "%s", filler.c_str());

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(LastMessage(), filler.substr(0, 16));
    EXPECT_EQ(sink.last_fit, EL_ERR_NO_SPACE);
}

TEST_F(ElLogPrintf, AMessageThatFillsTheSinksBufferExactlyIsWhole)
{
    recording_sink_set_capacity(&sink, 16);
    const std::string filler(16, 'y');
    el_log_printf(&log, EL_LOG_LEVEL_ERROR, "%s", filler.c_str());

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(LastMessage(), filler);
    EXPECT_EQ(sink.last_fit, EL_OK);
}

TEST_F(ElLogPrintf, ANumberThatDoesNotFitIsLeftOutNotCut)
{
    /* Two bytes of room for a five-digit number: by printf's item rule the
     * number is absent, and nothing follows it. */
    recording_sink_set_capacity(&sink, 16);
    const std::string filler(14, 'z');
    el_log_printf(&log, EL_LOG_LEVEL_ERROR, "%s%d!", filler.c_str(), 12345);

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(LastMessage(), filler);
    EXPECT_EQ(sink.last_fit, EL_ERR_NO_SPACE);
}

TEST_F(ElLogPrintf, NothingIsFormattedWhenTheSinkDoesNotBeginTheRecord)
{
    sink.begin_answer = EL_ERR_NO_SPACE;
    /* A null %s is a contract violation if printf ever reads it. */
    const char *volatile never_read = nullptr;
    el_log_printf(&log, EL_LOG_LEVEL_ERROR, "%s", never_read);

    EXPECT_EQ(sink.begins, 1u);
    EXPECT_EQ(sink.calls, 0u);
    EXPECT_EQ(clock.now_calls, 1u) << "the record was made, and is lost";
}

TEST_F(ElLogPrintf, ARecordIsBegunBeforeItsMessageIsFormatted)
{
    clock.reading = 9;
    el_log_printf(&log, EL_LOG_LEVEL_WARN, "%u", 5u);

    EXPECT_EQ(sink.begun.message, nullptr);
    EXPECT_EQ(sink.begun.len, 0u);
    EXPECT_EQ(sink.begun.timestamp, 9u);
    EXPECT_EQ(sink.begun.level, EL_LOG_LEVEL_WARN);
    EXPECT_EQ(LastMessage(), "5");
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

/* ---- Composing a message --------------------------------------------------- */

namespace
{

/* What a compose function was called with and saw, and what it answers: it
 * appends `text`, then returns `answer` (a failed append's status first). */
struct Composition {
    const char *text = "composed";
    el_status answer = EL_OK;
    unsigned calls = 0;
    void *ctx_seen = nullptr;
    el_buf *buf_seen = nullptr;
    size_t length_at_call = 0;
    const recording_sink *sink = nullptr; /* looked at from inside the call */
    unsigned begins_seen = 0;
    unsigned ends_seen = 0;
};

el_status Compose(void *ctx, el_buf *message)
{
    Composition *const c = static_cast<Composition *>(ctx);
    c->calls++;
    c->ctx_seen = ctx;
    c->buf_seen = message;
    c->length_at_call = el_buf_len(message);
    if (c->sink != nullptr) {
        c->begins_seen = c->sink->begins;
        c->ends_seen = c->sink->calls;
    }
    const el_status appended = el_buf_append(message, c->text, std::strlen(c->text));
    return appended != EL_OK ? appended : c->answer;
}

/* Appends nothing, and does not use its context. */
el_status ComposeNothing(void *ctx, el_buf *message)
{
    (void)message;
    EXPECT_EQ(ctx, nullptr);
    return EL_OK;
}

/* A printf of the application's own, into the buffer: `ctx` is the text. */
el_status ComposeFormatted(void *ctx, el_buf *message)
{
    return el_fmt_printf(message, "%s", static_cast<const char *>(ctx));
}

/* A formatter that works in place: reserve, write, commit. */
el_status ComposeInPlace(void *ctx, el_buf *message)
{
    (void)ctx;
    EL_TRY(el_buf_reserve(message, 4));
    std::memcpy(el_buf_spare(message), "abcd", 4);
    el_buf_commit(message, 4);
    return EL_OK;
}

/* Appends, then takes back what it appended and no more. */
el_status ComposeThenTakeBack(void *ctx, el_buf *message)
{
    (void)ctx;
    const size_t start = el_buf_len(message);
    EL_TRY(el_buf_append(message, "never kept", 10));
    el_buf_truncate(message, start);
    return EL_OK;
}

/* Logs a record through the logger in `ctx`. */
struct Inner {
    const el_logger *logger;
};

el_status ComposeAndLog(void *ctx, el_buf *message)
{
    const Inner *const inner = static_cast<const Inner *>(ctx);
    el_log_write(inner->logger, EL_LOG_LEVEL_INFO, "inner", 5);
    return el_buf_append(message, "outer", 5);
}

/* Silences the logger in `ctx` and then writes its message: a component
 * that turns its own logging off from inside a record. */
el_status ComposeAndSilence(void *ctx, el_buf *message)
{
    el_logger *const logger = static_cast<el_logger *>(ctx);
    *logger = *el_logger_off();
    return el_buf_append(message, "last", 4);
}

} // namespace

TEST_F(ElLogger, ComposeWritesTheMessageIntoTheBufferTheSinkLentAndEndsTheRecord)
{
    Composition c;
    clock.reading = 31;
    el_log_compose(&log, EL_LOG_LEVEL_WARN, Compose, &c);

    ASSERT_EQ(c.calls, 1u);
    EXPECT_EQ(c.ctx_seen, &c) << "the context goes through unchanged";
    EXPECT_EQ(c.buf_seen, &sink.buf) << "the buffer the sink lent";
    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(LastMessage(), "composed");
    EXPECT_EQ(sink.last_fit, EL_OK);
    EXPECT_EQ(sink.last_message_pointer, sink.storage) << "no copy on the way";
    EXPECT_EQ(sink.last.level, EL_LOG_LEVEL_WARN);
    EXPECT_STREQ(sink.last.tag, "net");
    EXPECT_EQ(sink.last.timestamp, 31u);
    EXPECT_EQ(sink.last.ticks_per_second, 1000u);
}

TEST_F(ElLogger, ComposeReadsTheClockOncePerRecordMadeAndBeginsBeforeItComposes)
{
    Composition c;
    c.sink = &sink;
    for (unsigned i = 1; i <= 3; i++) {
        clock.reading = i * 10u;
        el_log_compose(&log, EL_LOG_LEVEL_ERROR, Compose, &c);
        EXPECT_EQ(clock.now_calls, i);
        EXPECT_EQ(sink.begun.timestamp, i * 10u);
        EXPECT_EQ(sink.begun.message, nullptr);
        EXPECT_EQ(c.begins_seen, i) << "the sink had begun the record";
        EXPECT_EQ(c.ends_seen, i - 1) << "and had not ended it";
        EXPECT_EQ(sink.calls, i);
    }
}

TEST_F(ElLogger, ComposeBelowTheMinimumReadsNoClockAndCallsNothing)
{
    Composition c;
    el_log_compose(&log, EL_LOG_LEVEL_DEBUG, Compose, &c);

    EXPECT_EQ(c.calls, 0u);
    EXPECT_EQ(sink.begins, 0u);
    EXPECT_EQ(sink.calls, 0u);
    EXPECT_EQ(clock.now_calls, 0u) << "the clock is read only for a record that is made";

    el_logger_set_level(&log, EL_LOG_LEVEL_OFF);
    el_log_compose(&log, EL_LOG_LEVEL_ERROR, Compose, &c);
    EXPECT_EQ(c.calls, 0u);
    EXPECT_EQ(clock.now_calls, 0u);
}

TEST_F(ElLogger, ComposeIsNotCalledForARecordTheSinkDoesNotBegin)
{
    Composition c;
    sink.begin_answer = EL_ERR_NO_SPACE;
    el_log_compose(&log, EL_LOG_LEVEL_ERROR, Compose, &c);

    EXPECT_EQ(sink.begins, 1u) << "the record was made, and is lost";
    EXPECT_EQ(sink.calls, 0u) << "a record that was not begun is not ended";
    EXPECT_EQ(c.calls, 0u);
    EXPECT_EQ(clock.now_calls, 1u);

    sink.begin_answer = EL_OK;
    el_log_compose(&log, EL_LOG_LEVEL_ERROR, Compose, &c);
    EXPECT_EQ(c.calls, 1u);
    EXPECT_EQ(LastMessage(), "composed");
}

TEST_F(ElLogger, ComposeAcceptsANullContext)
{
    el_log_compose(&log, EL_LOG_LEVEL_ERROR, ComposeNothing, nullptr);
    EXPECT_EQ(sink.calls, 1u);
}

TEST_F(ElLogger, AnyStatusButOkIsACutMessageAndReachesTheSinkAsNoSpace)
{
    /* Elemental's codes, one the application made up, and the extremes. */
    const el_status statuses[] = {
        EL_ERR_NO_SPACE, EL_ERR_IO, EL_ERR_WOULD_BLOCK, EL_ERR_END_OF_STREAM, 1, -7,
        INT32_MAX,       INT32_MIN};
    for (const el_status status : statuses) {
        SCOPED_TRACE(testing::Message() << "status " << status);
        Composition c;
        c.text = "part";
        c.answer = status;
        const unsigned ended = sink.calls;
        el_log_compose(&log, EL_LOG_LEVEL_ERROR, Compose, &c);

        EXPECT_EQ(sink.calls, ended + 1) << "a cut record is still ended";
        EXPECT_EQ(sink.last_fit, EL_ERR_NO_SPACE) << "whatever the failure was";
        EXPECT_EQ(LastMessage(), "part") << "what was written before it is there";
    }
}

TEST_F(ElLogger, AMessageTheBufferCannotTakeIsCutAndTheSinkIsTold)
{
    recording_sink_set_capacity(&sink, 5);
    char text[] = "0123456789";
    el_log_compose(&log, EL_LOG_LEVEL_ERROR, ComposeFormatted, text);

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(LastMessage(), "01234");
    EXPECT_EQ(sink.last_fit, EL_ERR_NO_SPACE);
}

TEST_F(ElLogger, AMessageThatFillsTheBufferExactlyIsWhole)
{
    recording_sink_set_capacity(&sink, 5);
    char text[] = "01234";
    el_log_compose(&log, EL_LOG_LEVEL_ERROR, ComposeFormatted, text);

    EXPECT_EQ(LastMessage(), "01234");
    EXPECT_EQ(sink.last_fit, EL_OK);
}

TEST_F(ElLogger, ComposeAppendsAfterWhatTheSinkAlreadyHasInItsBuffer)
{
    char storage[32];
    el_buf lent;
    el_buf_init(&lent, storage, sizeof(storage));
    ASSERT_EQ(el_buf_append(&lent, "pre:", 4), EL_OK);
    sink.lend = &lent;

    Composition c;
    el_log_compose(&log, EL_LOG_LEVEL_ERROR, Compose, &c);

    EXPECT_EQ(c.length_at_call, 4u);
    EXPECT_EQ(sink.last_message_pointer, storage + 4);
    EXPECT_EQ(LastMessage(), "composed");
    EXPECT_EQ(std::string(storage, el_buf_len(&lent)), "pre:composed");
}

TEST_F(ElLogger, AnEmptyComposedMessageIsARecord)
{
    el_log_compose(&log, EL_LOG_LEVEL_INFO, ComposeNothing, nullptr);

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(sink.last.len, 0u);
    EXPECT_EQ(sink.last_message_pointer, nullptr);
    EXPECT_EQ(sink.last_fit, EL_OK);
}

TEST_F(ElLogger, ComposeMayTakeBackWhatItAppendedDownToTheLengthItStartedWith)
{
    char storage[32];
    el_buf lent;
    el_buf_init(&lent, storage, sizeof(storage));
    ASSERT_EQ(el_buf_append(&lent, "pre:", 4), EL_OK);
    sink.lend = &lent;

    el_log_compose(&log, EL_LOG_LEVEL_INFO, ComposeThenTakeBack, nullptr);

    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(sink.last.len, 0u);
    EXPECT_EQ(sink.last_fit, EL_OK);
}

TEST_F(ElLogger, ComposeMayWriteInPlaceWithReserveAndCommit)
{
    el_log_compose(&log, EL_LOG_LEVEL_INFO, ComposeInPlace, nullptr);

    EXPECT_EQ(LastMessage(), "abcd");
    EXPECT_EQ(sink.last_message_pointer, sink.storage);
}

TEST_F(ElLogger, ComposeMayLogToAnotherSink)
{
    recording_sink other;
    recording_sink_init(&other);
    el_logger other_log;
    el_logger_init(&other_log, recording_sink_as_sink(&other), counting_clock_as_clock(&clock),
                   "other", EL_LOG_LEVEL_INFO);
    Inner inner = {&other_log};

    el_log_compose(&log, EL_LOG_LEVEL_INFO, ComposeAndLog, &inner);

    ASSERT_EQ(other.calls, 1u);
    EXPECT_EQ(std::string(other.message, other.last.len), "inner");
    ASSERT_EQ(sink.calls, 1u);
    EXPECT_EQ(LastMessage(), "outer");
    EXPECT_EQ(sink.last_fit, EL_OK);
}

TEST_F(ElLogger, ARecordEndsOnTheSinkThatBeganItEvenIfComposeChangesTheLogger)
{
    el_log_compose(&log, EL_LOG_LEVEL_INFO, ComposeAndSilence, &log);

    EXPECT_EQ(log.level_min, EL_LOG_LEVEL_OFF) << "the logger was changed";
    ASSERT_EQ(sink.begins, 1u);
    ASSERT_EQ(sink.calls, 1u) << "the sink that began the record ends it";
    EXPECT_EQ(LastMessage(), "last");
    EXPECT_EQ(sink.last_fit, EL_OK);

    /* The sink is free for the next record, from another logger. */
    el_logger other;
    el_logger_init(&other, recording_sink_as_sink(&sink), counting_clock_as_clock(&clock), "other",
                   EL_LOG_LEVEL_INFO);
    el_log_write(&other, EL_LOG_LEVEL_INFO, "next", 4);
    EXPECT_EQ(sink.begins, 2u);
    ASSERT_EQ(sink.calls, 2u);
    EXPECT_EQ(LastMessage(), "next");
}

TEST_F(ElLogger, AMinimumOfOffReadsNoClockAndBeginsNothingFromAnyCallAtAnyLevel)
{
    el_logger_set_level(&log, EL_LOG_LEVEL_OFF);

    Composition c;
    /* A null %s is a contract violation if printf ever reads it. */
    const char *volatile never_read = nullptr;
    for (el_log_level level = EL_LOG_LEVEL_DEBUG; level < EL_LOG_LEVEL_OFF; level++) {
        el_log_write(&log, level, "x", 1);
        el_log_printf(&log, level, "%s", never_read);
        el_log_compose(&log, level, Compose, &c);
    }
    EXPECT_EQ(clock.now_calls, 0u);
    EXPECT_EQ(sink.begins, 0u);
    EXPECT_EQ(sink.calls, 0u);
    EXPECT_EQ(c.calls, 0u);
}

/* ---- The logger that makes no records ---------------------------------------- */

/* What the constant itself shows; that a minimum of OFF reads no clock and
 * begins no record is the logger test above, with a clock and a sink that
 * count. */
TEST(ElLoggerOff, IsOneConstantWithAMinimumOfOffThatFormatsAndComposesNothing)
{
    const el_logger *const off = el_logger_off();
    ASSERT_NE(off, nullptr);
    EXPECT_EQ(off, el_logger_off()) << "one constant for the whole program";
    EXPECT_EQ(off->level_min, EL_LOG_LEVEL_OFF);

    Composition c;
    /* A null %s is a contract violation if printf ever reads it. */
    const char *volatile never_read = nullptr;
    for (el_log_level level = EL_LOG_LEVEL_DEBUG; level < EL_LOG_LEVEL_OFF; level++) {
        el_log_write(off, level, "x", 1);
        el_log_printf(off, level, "%s", never_read);
        el_log_compose(off, level, Compose, &c);
    }
    EXPECT_EQ(c.calls, 0u);
}

TEST(ElLoggerOff, ACopyWithItsLevelLoweredLosesEveryRecordAndReadsAClockOfZero)
{
    el_logger copy = *el_logger_off();
    el_logger_set_level(&copy, EL_LOG_LEVEL_DEBUG);

    EXPECT_EQ(el_clock_now(copy.clock), 0u);
    EXPECT_EQ(copy.ticks_per_second, 1u);
    EXPECT_STREQ(copy.tag, "");

    /* Every record is lost where it begins: nothing is written, nothing is
     * composed, and no check fires. */
    Composition c;
    const char *volatile never_read = nullptr;
    for (el_log_level level = EL_LOG_LEVEL_DEBUG; level < EL_LOG_LEVEL_OFF; level++) {
        el_log_write(&copy, level, "x", 1);
        el_log_printf(&copy, level, "%s", never_read);
        el_log_compose(&copy, level, Compose, &c);
    }
    EXPECT_EQ(c.calls, 0u);

    /* And the constant itself is untouched by the copy's change. */
    EXPECT_EQ(el_logger_off()->level_min, EL_LOG_LEVEL_OFF);
}
