/*
 * What only the two sinks promise: the writer sink's line (elemental/
 * log_writer.h) and the memory sink's storage (elemental/log_mem.h). What
 * every sink promises is the contract suite (contract_sinks_test.cpp).
 */
#include <gtest/gtest.h>

#include <elemental/log.h>
#include <elemental/log_mem.h>
#include <elemental/log_printf.h>
#include <elemental/log_writer.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "log_support.h"

namespace
{

el_log_record Record(el_ticks timestamp, uint32_t ticks_per_second, el_log_level level,
                     const char *tag, const char *message)
{
    el_log_record record;
    record.timestamp = timestamp;
    record.ticks_per_second = ticks_per_second;
    record.level = level;
    record.tag = tag;
    record.message = message;
    record.len = std::strlen(message);
    return record;
}

class ElLogWriterSink : public testing::Test
{
  protected:
    void SetUp() override
    {
        scripted_writer_init(&writer);
        Open(sizeof(line_storage));
    }

    /* A sink over the scripted writer, its line in `line_cap` bytes of
     * fixed storage. */
    void Open(size_t line_cap)
    {
        el_buf_init(&line, line_storage, line_cap);
        el_log_writer_sink_init(&sink, scripted_writer_as_writer(&writer), &line);
        handle = el_log_writer_sink_as_sink(&sink);
    }

    std::string Written() const
    {
        return std::string(writer.bytes, writer.len);
    }

    /* The line one record becomes, with plenty of storage. */
    std::string LineOf(const el_log_record &record)
    {
        scripted_writer_init(&writer);
        Open(sizeof(line_storage));
        EXPECT_EQ(el_log_sink_write(handle, &record), EL_OK);
        return Written();
    }

    scripted_writer writer;
    char line_storage[200];
    el_buf line;
    el_log_writer_sink sink;
    el_log_sink handle;
};

class ElLogMemSink : public testing::Test
{
  protected:
    void SetUp() override
    {
        Open(storage, sizeof(storage));
    }

    void Open(void *at, size_t cap)
    {
        el_log_mem_sink_init(&sink, at, cap);
        handle = el_log_mem_sink_as_sink(&sink);
    }

    el_status Write(const char *message, el_log_level level = EL_LOG_LEVEL_INFO)
    {
        const el_log_record record = Record(5, 1000, level, "mem", message);
        return el_log_sink_write(handle, &record);
    }

    std::string MessageAt(size_t index) const
    {
        el_log_record record;
        el_log_mem_sink_get(&sink, index, &record);
        return std::string(record.message, record.len);
    }

    alignas(8) uint8_t storage[512];
    el_log_mem_sink sink;
    el_log_sink handle;
};

} // namespace

/* ---- The writer sink ------------------------------------------------------ */

TEST_F(ElLogWriterSink, WritesTheDocumentedLine)
{
    EXPECT_EQ(LineOf(Record(120345, 10000, EL_LOG_LEVEL_WARN, "net", "retrying in 2 s")),
              "[    12.034500] W net: retrying in 2 s\n");
    EXPECT_EQ(LineOf(Record(2503, 10000, EL_LOG_LEVEL_INFO, "", "started")),
              "[     0.250300] I started\n");
}

TEST_F(ElLogWriterSink, EachLevelHasItsLetter)
{
    EXPECT_EQ(LineOf(Record(0, 1, EL_LOG_LEVEL_DEBUG, "t", "m")), "[     0.000000] D t: m\n");
    EXPECT_EQ(LineOf(Record(0, 1, EL_LOG_LEVEL_INFO, "t", "m")), "[     0.000000] I t: m\n");
    EXPECT_EQ(LineOf(Record(0, 1, EL_LOG_LEVEL_WARN, "t", "m")), "[     0.000000] W t: m\n");
    EXPECT_EQ(LineOf(Record(0, 1, EL_LOG_LEVEL_ERROR, "t", "m")), "[     0.000000] E t: m\n");
}

TEST_F(ElLogWriterSink, TheTimeIsSecondsAndMicrosecondsOfTheRecordsOwnClock)
{
    /* The same instant, counted by clocks of different frequencies. */
    EXPECT_EQ(LineOf(Record(1500, 1000, EL_LOG_LEVEL_INFO, "", "")), "[     1.500000] I \n");
    EXPECT_EQ(LineOf(Record(49152, 32768, EL_LOG_LEVEL_INFO, "", "")), "[     1.500000] I \n");
    EXPECT_EQ(LineOf(Record(1500000000, 1000000000, EL_LOG_LEVEL_INFO, "", "")),
              "[     1.500000] I \n");
    /* Microseconds are rounded down: a third of a second. */
    EXPECT_EQ(LineOf(Record(1, 3, EL_LOG_LEVEL_INFO, "", "")), "[     0.333333] I \n");
    EXPECT_EQ(LineOf(Record(999, 1000000000, EL_LOG_LEVEL_INFO, "", "")), "[     0.000000] I \n");
}

TEST_F(ElLogWriterSink, SecondsThatNeedMoreThanSixColumnsGetThem)
{
    EXPECT_EQ(LineOf(Record(999999, 1, EL_LOG_LEVEL_INFO, "", "x")), "[999999.000000] I x\n");
    EXPECT_EQ(LineOf(Record(1234567, 1, EL_LOG_LEVEL_INFO, "", "x")), "[1234567.000000] I x\n");
    EXPECT_EQ(LineOf(Record(UINT64_MAX, 1, EL_LOG_LEVEL_INFO, "", "x")),
              "[18446744073709551615.000000] I x\n");
}

TEST_F(ElLogWriterSink, AnEmptyMessageIsALine)
{
    EXPECT_EQ(LineOf(Record(0, 1, EL_LOG_LEVEL_ERROR, "boot", "")), "[     0.000000] E boot: \n");

    scripted_writer_init(&writer);
    el_log_record record = Record(0, 1, EL_LOG_LEVEL_ERROR, "", "");
    record.message = nullptr;
    EXPECT_EQ(el_log_sink_write(handle, &record), EL_OK);
    EXPECT_EQ(Written(), "[     0.000000] E \n");
}

TEST_F(ElLogWriterSink, TheMessageIsWrittenAsItIs)
{
    /* Not escaped, not trimmed: a newline in it makes two lines. */
    EXPECT_EQ(LineOf(Record(0, 1, EL_LOG_LEVEL_INFO, "t", "a: b\nc\t%d ")),
              "[     0.000000] I t: a: b\nc\t%d \n");
}

TEST_F(ElLogWriterSink, PassesEachLineOnWithOneWrite)
{
    const el_log_record first = Record(1, 1, EL_LOG_LEVEL_INFO, "t", "one");
    const el_log_record second = Record(2, 1, EL_LOG_LEVEL_INFO, "t", "two");

    ASSERT_EQ(el_log_sink_write(handle, &first), EL_OK);
    EXPECT_EQ(writer.calls, 1u) << "a line is one write to a writer that takes it whole";
    ASSERT_EQ(el_log_sink_write(handle, &second), EL_OK);
    EXPECT_EQ(writer.calls, 2u);
    EXPECT_EQ(Written(), "[     1.000000] I t: one\n[     2.000000] I t: two\n");
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 0u);
}

TEST_F(ElLogWriterSink, AWriterThatTakesAFewBytesAtATimeStillGetsTheWholeLine)
{
    writer.chunk = 3;
    const el_log_record record = Record(1, 1, EL_LOG_LEVEL_INFO, "t", "in pieces");

    EXPECT_EQ(el_log_sink_write(handle, &record), EL_OK);
    EXPECT_EQ(Written(), "[     1.000000] I t: in pieces\n");
    EXPECT_GT(writer.calls, 1u);
}

TEST_F(ElLogWriterSink, ALineThatDoesNotFitGoesOutCutAndEndsInANewline)
{
    const el_log_record record = Record(1, 1, EL_LOG_LEVEL_WARN, "t", "0123456789abcdef");
    const std::string whole = "[     1.000000] W t: 0123456789abcdef\n";

    for (size_t cap = 22; cap < whole.size(); cap++) {
        scripted_writer_init(&writer);
        Open(cap);
        EXPECT_EQ(el_log_sink_write(handle, &record), EL_ERR_NO_SPACE) << cap << " bytes";
        /* As much of the line as fits before the newline. */
        EXPECT_EQ(Written(), whole.substr(0, cap - 1) + "\n") << cap << " bytes";
        EXPECT_EQ(el_log_writer_sink_lost(&sink), 1u) << cap << " bytes";
    }
}

TEST_F(ElLogWriterSink, ALineThatFitsItsStorageExactlyIsWhole)
{
    const el_log_record record = Record(1, 1, EL_LOG_LEVEL_WARN, "t", "0123456789abcdef");
    const std::string whole = "[     1.000000] W t: 0123456789abcdef\n";

    Open(whole.size());
    EXPECT_EQ(el_log_sink_write(handle, &record), EL_OK);
    EXPECT_EQ(Written(), whole);
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 0u);
}

TEST_F(ElLogWriterSink, StorageTooSmallForThePrefixStillEndsEachLine)
{
    const el_log_record record = Record(1, 1, EL_LOG_LEVEL_WARN, "tag", "message");

    for (size_t cap = 1; cap < 22; cap++) {
        scripted_writer_init(&writer);
        Open(cap);
        EXPECT_EQ(el_log_sink_write(handle, &record), EL_ERR_NO_SPACE) << cap << " bytes";
        ASSERT_GE(writer.len, 1u) << cap << " bytes";
        EXPECT_LE(writer.len, cap) << cap << " bytes";
        EXPECT_EQ(writer.bytes[writer.len - 1], '\n') << cap << " bytes";
    }
}

TEST_F(ElLogWriterSink, WithNoStorageEveryRecordIsLostAndNothingIsWritten)
{
    el_buf_init(&line, nullptr, 0);
    el_log_writer_sink_init(&sink, scripted_writer_as_writer(&writer), &line);
    handle = el_log_writer_sink_as_sink(&sink);
    const el_log_record record = Record(1, 1, EL_LOG_LEVEL_WARN, "t", "m");

    EXPECT_EQ(el_log_sink_write(handle, &record), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_sink_write(handle, &record), EL_ERR_NO_SPACE);
    EXPECT_EQ(writer.len, 0u);
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 2u);
}

TEST_F(ElLogWriterSink, AFailingWritersStatusComesBackAndTheRecordCountsAsLost)
{
    const el_log_record record = Record(1, 1, EL_LOG_LEVEL_WARN, "t", "m");

    /* Elemental's codes and an application's own, unchanged. */
    const el_status failures[] = {EL_ERR_IO, EL_ERR_END_OF_STREAM, EL_ERR_NO_SPACE, -5};
    for (const el_status failure : failures) {
        scripted_writer_init(&writer);
        Open(sizeof(line_storage));
        writer.fail_after = 5;
        writer.failure = failure;

        EXPECT_EQ(el_log_sink_write(handle, &record), failure);
        /* What the writer took before it failed stays in the stream. */
        EXPECT_EQ(Written(), "[    ");
        EXPECT_EQ(el_log_writer_sink_lost(&sink), 1u);
    }
}

TEST_F(ElLogWriterSink, AfterAStallTheNextRecordStartsANewLine)
{
    const el_log_record first = Record(1, 1, EL_LOG_LEVEL_INFO, "t", "first");
    const el_log_record second = Record(2, 1, EL_LOG_LEVEL_INFO, "t", "second");

    writer.fail_after = 10;
    writer.failure = EL_ERR_WOULD_BLOCK;
    EXPECT_EQ(el_log_sink_write(handle, &first), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(Written(), "[     1.00");

    /* The stream moves again. The sink held nothing back: the rest of the
     * first line is not sent, and the second line is whole. */
    writer.fail_after = sizeof(writer.bytes);
    EXPECT_EQ(el_log_sink_write(handle, &second), EL_OK);
    EXPECT_EQ(Written(), "[     1.00[     2.000000] I t: second\n");
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 1u);
}

TEST_F(ElLogWriterSink, LostCountsEveryRecordThatDidNotGoOutWhole)
{
    const el_log_record fits = Record(1, 1, EL_LOG_LEVEL_INFO, "", "ok");
    const el_log_record too_long = Record(1, 1, EL_LOG_LEVEL_INFO, "", "0123456789012345678901");

    Open(32);
    EXPECT_EQ(el_log_sink_write(handle, &fits), EL_OK);
    EXPECT_EQ(el_log_sink_write(handle, &too_long), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_sink_write(handle, &fits), EL_OK);
    EXPECT_EQ(el_log_sink_write(handle, &too_long), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 2u);

    /* Setting the sink up again starts the count over. */
    Open(32);
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 0u);
}

TEST_F(ElLogWriterSink, APrintfMessageIsFormattedIntoTheLineAfterItsStart)
{
    counting_clock clock;
    counting_clock_init(&clock, 1000);
    clock.reading = 1500;
    el_logger log;
    el_logger_init(&log, handle, counting_clock_as_clock(&clock), "net", EL_LOG_LEVEL_DEBUG);

    EL_LOG_WARN(&log, "%s did not answer, attempt %d of %d", "10.0.0.7", 2, 3);
    EXPECT_EQ(Written(), "[     1.500000] W net: 10.0.0.7 did not answer, attempt 2 of 3\n");
    EXPECT_EQ(writer.calls, 1u);
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 0u);
}

TEST_F(ElLogWriterSink, APrintfMessageCutByTheLineGoesOutCutAndCountsAsLost)
{
    counting_clock clock;
    counting_clock_init(&clock, 1);
    el_logger log;
    el_logger_init(&log, handle, counting_clock_as_clock(&clock), "t", EL_LOG_LEVEL_DEBUG);
    const std::string whole = "[     0.000000] I t: 0123456789\n";

    Open(whole.size() - 4);
    EL_LOG_INFO(&log, "%s", "0123456789");
    EXPECT_EQ(Written(), whole.substr(0, whole.size() - 5) + "\n");
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 1u);

    /* A number that does not fit is left out, not cut. */
    scripted_writer_init(&writer);
    Open(whole.size() - 4);
    EL_LOG_INFO(&log, "%s%d", "012345", 6789);
    EXPECT_EQ(Written(), "[     0.000000] I t: 012345\n");
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 1u);
}

TEST_F(ElLogWriterSink, APrintfMessageAfterACutStartOfTheLineIsLeftOut)
{
    counting_clock clock;
    counting_clock_init(&clock, 1);
    el_logger log;
    el_logger_init(&log, handle, counting_clock_as_clock(&clock), "", EL_LOG_LEVEL_DEBUG);

    /* Room for "[" and three bytes, not for the seconds: the message would
     * fit in what is left, but nothing follows a cut item. */
    Open(4);
    EL_LOG_INFO(&log, "%s", "ab");
    EXPECT_EQ(Written(), "[\n");
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 1u);
}

/* ---- The memory sink ------------------------------------------------------ */

TEST_F(ElLogMemSink, KeepsARecordWithEveryField)
{
    static const char tag[] = "disk";
    const el_log_record written = Record(UINT64_MAX, UINT32_MAX, EL_LOG_LEVEL_ERROR, tag, "full");
    ASSERT_EQ(el_log_sink_write(handle, &written), EL_OK);

    ASSERT_EQ(el_log_mem_sink_count(&sink), 1u);
    el_log_record record;
    el_log_mem_sink_get(&sink, 0, &record);
    EXPECT_EQ(record.timestamp, UINT64_MAX);
    EXPECT_EQ(record.ticks_per_second, UINT32_MAX);
    EXPECT_EQ(record.level, EL_LOG_LEVEL_ERROR);
    EXPECT_EQ(record.tag, tag) << "the tag is kept by pointer";
    EXPECT_EQ(std::string(record.message, record.len), "full");
    EXPECT_EQ(el_log_mem_sink_lost(&sink), 0u);
}

TEST_F(ElLogMemSink, StartsEmpty)
{
    EXPECT_EQ(el_log_mem_sink_count(&sink), 0u);
    EXPECT_EQ(el_log_mem_sink_lost(&sink), 0u);
}

TEST_F(ElLogMemSink, ReturnsRecordsOldestFirstAtAnyIndex)
{
    ASSERT_EQ(Write("first", EL_LOG_LEVEL_DEBUG), EL_OK);
    ASSERT_EQ(Write("", EL_LOG_LEVEL_INFO), EL_OK);
    ASSERT_EQ(Write("the third one is longer", EL_LOG_LEVEL_WARN), EL_OK);
    ASSERT_EQ(Write("4", EL_LOG_LEVEL_ERROR), EL_OK);

    ASSERT_EQ(el_log_mem_sink_count(&sink), 4u);
    /* In any order, and more than once. */
    EXPECT_EQ(MessageAt(3), "4");
    EXPECT_EQ(MessageAt(0), "first");
    EXPECT_EQ(MessageAt(2), "the third one is longer");
    EXPECT_EQ(MessageAt(1), "");
    EXPECT_EQ(MessageAt(2), "the third one is longer");

    el_log_record record;
    el_log_mem_sink_get(&sink, 2, &record);
    EXPECT_EQ(record.level, EL_LOG_LEVEL_WARN);
}

TEST_F(ElLogMemSink, TheMessageItReturnsIsInItsOwnStorage)
{
    char message[] = "changes later";
    ASSERT_EQ(Write(message), EL_OK);
    message[0] = 'X';

    el_log_record record;
    el_log_mem_sink_get(&sink, 0, &record);
    EXPECT_EQ(std::string(record.message, record.len), "changes later");
    const uint8_t *const bytes = reinterpret_cast<const uint8_t *>(record.message);
    EXPECT_GE(bytes, storage);
    EXPECT_LT(bytes, storage + sizeof(storage));
}

TEST_F(ElLogMemSink, RecordSizeIsWhatOneRecordTakes)
{
    EXPECT_EQ(el_log_mem_sink_record_size(10) - el_log_mem_sink_record_size(0), 10u);

    /* Storage for exactly three records of seven bytes takes three. */
    Open(storage, 3 * el_log_mem_sink_record_size(7));
    EXPECT_EQ(Write("1234567"), EL_OK);
    EXPECT_EQ(Write("1234567"), EL_OK);
    EXPECT_EQ(Write("1234567"), EL_OK);
    EXPECT_EQ(Write(""), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_mem_sink_count(&sink), 3u);
}

TEST_F(ElLogMemSink, StorageSizeIsTheFunctionsRecordSizeForNRecords)
{
    const size_t counts[] = {0, 1, 3, 8};
    const size_t longest[] = {0, 1, 24, 64};
    for (const size_t n : counts) {
        for (const size_t m : longest) {
            EXPECT_EQ(EL_LOG_MEM_SINK_STORAGE_SIZE(n, m), n * el_log_mem_sink_record_size(m))
                << n << " records of " << m << " bytes";
        }
    }

    /* A constant expression, for a static array. */
    static uint8_t constant[EL_LOG_MEM_SINK_STORAGE_SIZE(3, 10)];
    EXPECT_EQ(sizeof(constant), 3 * el_log_mem_sink_record_size(10));
}

TEST_F(ElLogMemSink, StorageOfTheMacrosSizeKeepsNRecordsOfMBytesAndLosesTheNext)
{
    constexpr size_t kCount = 4;
    constexpr size_t kLongest = 9;
    static uint8_t exact[EL_LOG_MEM_SINK_STORAGE_SIZE(kCount, kLongest)];
    Open(exact, sizeof(exact));

    for (size_t i = 0; i < kCount; i++) {
        EXPECT_EQ(Write("123456789"), EL_OK) << "record " << i;
    }
    EXPECT_EQ(el_log_mem_sink_count(&sink), kCount);
    EXPECT_EQ(Write("123456789"), EL_ERR_NO_SPACE) << "the next one";
    EXPECT_EQ(Write(""), EL_ERR_NO_SPACE) << "not even an empty one";
    EXPECT_EQ(el_log_mem_sink_count(&sink), kCount);
    EXPECT_EQ(el_log_mem_sink_lost(&sink), 2u);
    EXPECT_EQ(MessageAt(kCount - 1), "123456789");
}

TEST_F(ElLogMemSink, ARecordThatDoesNotFitIsLostWholeAndCounted)
{
    Open(storage, el_log_mem_sink_record_size(5) + el_log_mem_sink_record_size(8) - 1);
    ASSERT_EQ(Write("12345"), EL_OK);

    EXPECT_EQ(Write("12345678"), EL_ERR_NO_SPACE) << "one byte short";
    EXPECT_EQ(el_log_mem_sink_count(&sink), 1u);
    EXPECT_EQ(el_log_mem_sink_lost(&sink), 1u);
    EXPECT_EQ(MessageAt(0), "12345") << "what was kept is untouched";

    /* Nothing was used up by the one that was lost: a shorter one fits. */
    EXPECT_EQ(Write("1234567"), EL_OK);
    EXPECT_EQ(MessageAt(1), "1234567");
    EXPECT_EQ(el_log_mem_sink_lost(&sink), 1u);
}

TEST_F(ElLogMemSink, StorageTooSmallForARecordsOwnFieldsLosesIt)
{
    Open(storage, el_log_mem_sink_record_size(0) - 1);

    EXPECT_EQ(Write(""), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_mem_sink_count(&sink), 0u);
    EXPECT_EQ(el_log_mem_sink_lost(&sink), 1u);

    /* Refused at begin, so no record is left open: another is begun, and
     * refused again, without a check firing. */
    EXPECT_EQ(Write("x"), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_mem_sink_lost(&sink), 2u);
}

TEST_F(ElLogMemSink, WithNoStorageEveryRecordIsLost)
{
    Open(nullptr, 0);

    EXPECT_EQ(Write("x"), EL_ERR_NO_SPACE);
    EXPECT_EQ(Write(""), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_mem_sink_count(&sink), 0u);
    EXPECT_EQ(el_log_mem_sink_lost(&sink), 2u);
}

TEST_F(ElLogMemSink, ClearForgetsTheRecordsAndTheLostCountAndKeepsTheStorage)
{
    Open(storage, el_log_mem_sink_record_size(4));
    ASSERT_EQ(Write("1234"), EL_OK);
    ASSERT_EQ(Write("1234"), EL_ERR_NO_SPACE);

    el_log_mem_sink_clear(&sink);
    EXPECT_EQ(el_log_mem_sink_count(&sink), 0u);
    EXPECT_EQ(el_log_mem_sink_lost(&sink), 0u);

    EXPECT_EQ(Write("abcd"), EL_OK);
    EXPECT_EQ(MessageAt(0), "abcd");
}

TEST_F(ElLogMemSink, APrintfMessageIsFormattedWhereItIsKept)
{
    counting_clock clock;
    counting_clock_init(&clock, 1000);
    clock.reading = 3;
    el_logger log;
    el_logger_init(&log, handle, counting_clock_as_clock(&clock), "mem", EL_LOG_LEVEL_DEBUG);

    EL_LOG_ERROR(&log, "%d of %d", 1, 2);
    EL_LOG_INFO(&log, "%s", "second");

    ASSERT_EQ(el_log_mem_sink_count(&sink), 2u);
    el_log_record record;
    el_log_mem_sink_get(&sink, 0, &record);
    EXPECT_EQ(std::string(record.message, record.len), "1 of 2");
    EXPECT_EQ(record.level, EL_LOG_LEVEL_ERROR);
    EXPECT_EQ(record.timestamp, 3u);
    /* Right after its own fields: kept where it was formatted. */
    EXPECT_EQ(reinterpret_cast<const uint8_t *>(record.message),
              storage + el_log_mem_sink_record_size(0));
    EXPECT_EQ(MessageAt(1), "second");
}

TEST_F(ElLogMemSink, APrintfMessageThatDoesNotFitIsLostWhole)
{
    Open(storage, el_log_mem_sink_record_size(4) + el_log_mem_sink_record_size(6) - 1);
    counting_clock clock;
    counting_clock_init(&clock, 1000);
    el_logger log;
    el_logger_init(&log, handle, counting_clock_as_clock(&clock), "mem", EL_LOG_LEVEL_DEBUG);

    EL_LOG_INFO(&log, "%d", 1234);
    EL_LOG_INFO(&log, "%d", 123456);
    EXPECT_EQ(el_log_mem_sink_count(&sink), 1u);
    EXPECT_EQ(el_log_mem_sink_lost(&sink), 1u);

    /* Nothing was used up by the one that was lost. */
    EL_LOG_INFO(&log, "%d", 12345);
    ASSERT_EQ(el_log_mem_sink_count(&sink), 2u);
    EXPECT_EQ(MessageAt(0), "1234");
    EXPECT_EQ(MessageAt(1), "12345");
}

TEST_F(ElLogMemSink, WorksInStorageOfAnyAlignment)
{
    for (size_t offset = 1; offset < 8; offset++) {
        Open(storage + offset, 200);
        ASSERT_EQ(Write("unaligned"), EL_OK) << "offset " << offset;
        ASSERT_EQ(Write("storage"), EL_OK) << "offset " << offset;

        el_log_record record;
        el_log_mem_sink_get(&sink, 1, &record);
        EXPECT_EQ(std::string(record.message, record.len), "storage") << "offset " << offset;
        EXPECT_EQ(record.timestamp, 5u) << "offset " << offset;
        EXPECT_STREQ(record.tag, "mem") << "offset " << offset;
    }
}
