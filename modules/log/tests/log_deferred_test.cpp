/*
 * What only the deferred sink promises (elemental/log_deferred.h): a record
 * waits in the sink's queue, whole, until it is drained; one that does not
 * fit is lost whole and counted; a drain is bounded by what was there. One
 * context drives both sides here; two are log_deferred_stress_test.cpp. What
 * every sink promises is the contract suite (contract_deferred_test.cpp).
 */
#include <gtest/gtest.h>

#include <elemental/log.h>
#include <elemental/log_deferred.h>
#include <elemental/log_mem.h>
#include <elemental/log_printf.h>
#include <elemental/time_manual.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "log_support.h"

namespace
{

/* Static, not in the fixture: a Zephyr image runs the tests on a 4 KiB
 * stack and a small heap. One byte more than is used, for the tests that
 * want storage of an odd alignment. */
constexpr size_t kMessageMax = 96;
constexpr size_t kQueueMax = 512;
alignas(8) uint8_t storage[EL_LOG_DEFERRED_SINK_STORAGE_SIZE(1, kMessageMax) + kQueueMax + 1];
alignas(8) uint8_t kept_storage[1024];
recording_sink recording;

el_log_record Record(const char *message, el_ticks timestamp = 5, uint32_t ticks_per_second = 1000,
                     el_log_level level = EL_LOG_LEVEL_INFO, const char *tag = "deferred")
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

class ElLogDeferredSink : public testing::Test
{
  protected:
    void SetUp() override
    {
        Open(kQueueMax);
        el_log_mem_sink_init(&kept, kept_storage, sizeof(kept_storage));
        to = el_log_mem_sink_as_sink(&kept);
        recording_sink_init(&recording);
    }

    /* A sink over a queue of `queue_cap` bytes, building its records in
     * room for a message of `message_max` bytes. */
    void Open(size_t queue_cap, size_t message_max = kMessageMax)
    {
        const size_t cap = el_log_deferred_sink_record_size(message_max) + queue_cap;
        ASSERT_LE(cap, sizeof(storage) - 1);
        el_log_deferred_sink_init(&sink, storage, cap, message_max);
        handle = el_log_deferred_sink_as_sink(&sink);
    }

    el_status Write(const char *message)
    {
        const el_log_record record = Record(message);
        return el_log_sink_write(handle, &record);
    }

    size_t Drain()
    {
        return el_log_deferred_sink_drain(&sink, to);
    }

    std::string KeptMessage(size_t index) const
    {
        el_log_record record;
        el_log_mem_sink_get(&kept, index, &record);
        return std::string(record.message, record.len);
    }

    el_log_deferred_sink sink;
    el_log_sink handle;
    el_log_mem_sink kept;
    el_log_sink to;
};

TEST_F(ElLogDeferredSink, ARecordWaitsInTheQueueUntilItIsDrained)
{
    EXPECT_EQ(Write("first"), EL_OK);
    EXPECT_EQ(el_log_mem_sink_count(&kept), 0u) << "the record went on before a drain";
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), el_log_deferred_sink_record_size(5));

    EXPECT_EQ(Drain(), 1u);
    ASSERT_EQ(el_log_mem_sink_count(&kept), 1u);
    EXPECT_EQ(KeptMessage(0), "first");
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), 0u);
}

TEST_F(ElLogDeferredSink, ARecordArrivesWithEveryField)
{
    static const char tag[] = "uart";
    const el_log_record record =
        Record("overrun", 0x123456789abcULL, 32768, EL_LOG_LEVEL_WARN, tag);
    ASSERT_EQ(el_log_sink_write(handle, &record), EL_OK);
    ASSERT_EQ(Drain(), 1u);

    el_log_record back;
    el_log_mem_sink_get(&kept, 0, &back);
    EXPECT_EQ(back.timestamp, 0x123456789abcULL);
    EXPECT_EQ(back.ticks_per_second, 32768u);
    EXPECT_EQ(back.level, EL_LOG_LEVEL_WARN);
    EXPECT_EQ(back.tag, tag) << "the tag travels as the logger's pointer";
    EXPECT_EQ(std::string(back.message, back.len), "overrun");
}

/* One sink may serve loggers with different clocks: the frequency is each
 * record's own. */
TEST_F(ElLogDeferredSink, EachRecordKeepsItsOwnClocksFrequency)
{
    const el_log_record fast = Record("a", 10, 1000000);
    const el_log_record slow = Record("b", 20, 100);
    ASSERT_EQ(el_log_sink_write(handle, &fast), EL_OK);
    ASSERT_EQ(el_log_sink_write(handle, &slow), EL_OK);
    ASSERT_EQ(Drain(), 2u);

    el_log_record back;
    el_log_mem_sink_get(&kept, 0, &back);
    EXPECT_EQ(back.ticks_per_second, 1000000u);
    EXPECT_EQ(back.timestamp, 10u);
    el_log_mem_sink_get(&kept, 1, &back);
    EXPECT_EQ(back.ticks_per_second, 100u);
    EXPECT_EQ(back.timestamp, 20u);
}

TEST_F(ElLogDeferredSink, DrainGivesRecordsOldestFirstAndSaysHowMany)
{
    ASSERT_EQ(Write("one"), EL_OK);
    ASSERT_EQ(Write(""), EL_OK);
    ASSERT_EQ(Write("three"), EL_OK);

    EXPECT_EQ(Drain(), 3u);
    ASSERT_EQ(el_log_mem_sink_count(&kept), 3u);
    EXPECT_EQ(KeptMessage(0), "one");
    EXPECT_EQ(KeptMessage(1), "");
    EXPECT_EQ(KeptMessage(2), "three");
}

TEST_F(ElLogDeferredSink, DrainingAnEmptyQueueCallsNobody)
{
    EXPECT_EQ(el_log_deferred_sink_drain(&sink, recording_sink_as_sink(&recording)), 0u);
    EXPECT_EQ(recording.begins, 0u);

    ASSERT_EQ(Write("x"), EL_OK);
    EXPECT_EQ(el_log_deferred_sink_drain(&sink, recording_sink_as_sink(&recording)), 1u);
    EXPECT_EQ(el_log_deferred_sink_drain(&sink, recording_sink_as_sink(&recording)), 0u);
    EXPECT_EQ(recording.begins, 1u);
}

/* A queue that no whole number of these records fills: each lap, a record
 * lies across the end of the storage, in its fields or in its message. */
TEST_F(ElLogDeferredSink, ARecordAcrossTheEndOfTheQueueArrivesWhole)
{
    const size_t one = el_log_deferred_sink_record_size(11);
    Open(2 * one + 5);

    for (unsigned i = 0; i < 100; i++) {
        SCOPED_TRACE(testing::Message() << "record " << i);
        const std::string message = "message " + std::to_string(100 + i);
        const el_log_record record = Record(message.c_str(), i);
        ASSERT_EQ(el_log_sink_write(handle, &record), EL_OK);

        ASSERT_EQ(el_log_deferred_sink_drain(&sink, recording_sink_as_sink(&recording)), 1u);
        ASSERT_EQ(recording.calls, i + 1);
        EXPECT_EQ(recording.last.timestamp, i);
        EXPECT_EQ(recording.last.ticks_per_second, 1000u);
        EXPECT_EQ(recording.last.level, EL_LOG_LEVEL_INFO);
        EXPECT_STREQ(recording.last.tag, "deferred");
        EXPECT_EQ(std::string(recording.last.message, recording.last.len), message);
        EXPECT_EQ(recording.last_fit, EL_OK);
    }
}

TEST_F(ElLogDeferredSink, RecordSizeIsWhatOneRecordTakesInTheQueueAndInTheRecordArea)
{
    EXPECT_EQ(el_log_deferred_sink_record_size(10) - el_log_deferred_sink_record_size(0), 10u);

    /* In the queue. */
    const size_t one = el_log_deferred_sink_record_size(4);
    Open(3 * one);
    EXPECT_EQ(Write("aaaa"), EL_OK);
    EXPECT_EQ(Write("bbbb"), EL_OK);
    EXPECT_EQ(Write("cccc"), EL_OK);
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), 3 * one);
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 0u);
    EXPECT_EQ(Write(""), EL_ERR_NO_SPACE) << "not even the fields of another fit";

    /* In the record area: exactly enough for a message of four bytes. */
    Open(kQueueMax, 4);
    EXPECT_EQ(Write("dddd"), EL_OK);
    EXPECT_EQ(Write("eeeee"), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 1u);
}

TEST_F(ElLogDeferredSink, AMessageLongerThanTheRecordAreaIsLostWholeAndCounted)
{
    Open(kQueueMax, 8);

    EXPECT_EQ(Write("12345678"), EL_OK);
    EXPECT_EQ(Write("123456789"), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 1u);
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), el_log_deferred_sink_record_size(8))
        << "part of a lost record is in the queue";

    /* What came before and what comes after is whole. */
    EXPECT_EQ(Write("abc"), EL_OK);
    EXPECT_EQ(Drain(), 2u);
    EXPECT_EQ(KeptMessage(0), "12345678");
    EXPECT_EQ(KeptMessage(1), "abc");
}

TEST_F(ElLogDeferredSink, ARecordTheQueueHasNoRoomForIsLostWholeAndCounted)
{
    const size_t one = el_log_deferred_sink_record_size(6);
    /* Two records, and all but one byte of a third. */
    Open(3 * one - 1);

    EXPECT_EQ(Write("first."), EL_OK);
    EXPECT_EQ(Write("second"), EL_OK);
    EXPECT_EQ(Write("third."), EL_ERR_NO_SPACE);
    EXPECT_EQ(Write("fourth"), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 2u);
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), 2 * one)
        << "part of a lost record is in the queue";

    /* A shorter one still fits in what is left. */
    EXPECT_EQ(Write("short"), EL_OK);

    /* Nothing kept was overwritten, and the room comes back. */
    EXPECT_EQ(Drain(), 3u);
    EXPECT_EQ(KeptMessage(0), "first.");
    EXPECT_EQ(KeptMessage(1), "second");
    EXPECT_EQ(KeptMessage(2), "short");
    EXPECT_EQ(Write("third."), EL_OK);
    EXPECT_EQ(Drain(), 1u);
    EXPECT_EQ(KeptMessage(3), "third.");
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 2u);
}

/* Refused at begin, before anything is formatted: the queue's room does not
 * hold even the record's own fields. */
TEST_F(ElLogDeferredSink, AQueueWithoutRoomForARecordsOwnFieldsLosesIt)
{
    Open(el_log_deferred_sink_record_size(0) - 1);

    /* Begin itself refuses: it lends no buffer to format into. */
    el_log_record record = Record("");
    record.message = nullptr;
    el_buf *message = nullptr;
    EXPECT_EQ(handle.ops->begin(handle.ctx, &record, &message), EL_ERR_NO_SPACE);
    EXPECT_EQ(message, nullptr);
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 1u);

    EXPECT_EQ(Write(""), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 2u);
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), 0u);
}

/* A message of no bytes: the record area is the fields and nothing else, so
 * only an empty message is built, and kept. */
TEST_F(ElLogDeferredSink, ASinkForEmptyMessagesTakesEmptyRecordsAndLosesTheRest)
{
    Open(kQueueMax, 0);
    EXPECT_EQ(Write(""), EL_OK);
    EXPECT_EQ(Write("x"), EL_ERR_NO_SPACE);
    EXPECT_EQ(Drain(), 1u);
    EXPECT_EQ(KeptMessage(0), "");
}

/* Storage of one record's worth and nothing more: no room in the queue
 * even for the fields of a record, so nothing can wait. */
TEST_F(ElLogDeferredSink, StorageOfOneRecordsWorthLosesEveryRecord)
{
    Open(0, 8);
    EXPECT_EQ(Write(""), EL_ERR_NO_SPACE);
    EXPECT_EQ(Write("12345678"), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 2u);
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), 0u);
}

TEST_F(ElLogDeferredSink, WithNoStorageEveryRecordIsLost)
{
    el_log_deferred_sink_init(&sink, nullptr, 0, kMessageMax);
    handle = el_log_deferred_sink_as_sink(&sink);

    EXPECT_EQ(Write(""), EL_ERR_NO_SPACE);
    EXPECT_EQ(Write("message"), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 2u);
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), 0u);
    EXPECT_EQ(Drain(), 0u);
}

TEST_F(ElLogDeferredSink, SettingTheSinkUpAgainForgetsTheLostCount)
{
    Open(0);
    EXPECT_EQ(Write("x"), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 1u);

    Open(kQueueMax);
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 0u);
}

/* The record a destination refuses is taken out of the queue, message and
 * all, so the next one is read from where it starts. */
TEST_F(ElLogDeferredSink, ADestinationThatRefusesARecordStillGetsTheNextOneWhole)
{
    ASSERT_EQ(Write("refused: a message longer than the sixteen bytes dropped at a time"), EL_OK);
    ASSERT_EQ(Write("taken"), EL_OK);

    recording.begin_answer = EL_ERR_IO;
    const el_log_sink refusing = recording_sink_as_sink(&recording);
    /* Bounded by what was there: both are taken out, and both refused. */
    EXPECT_EQ(el_log_deferred_sink_drain(&sink, refusing), 2u);
    EXPECT_EQ(recording.begins, 2u);
    EXPECT_EQ(recording.calls, 0u) << "a record that was not begun was ended";
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), 0u);
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 0u) << "lost by the destination, not by this sink";

    recording.begin_answer = EL_OK;
    ASSERT_EQ(Write("after"), EL_OK);
    EXPECT_EQ(el_log_deferred_sink_drain(&sink, refusing), 1u);
    EXPECT_EQ(std::string(recording.last.message, recording.last.len), "after");
}

TEST_F(ElLogDeferredSink, ADestinationWhoseEndFailsStillGetsTheNextOne)
{
    ASSERT_EQ(Write("one"), EL_OK);
    ASSERT_EQ(Write("two"), EL_OK);

    recording.answer = EL_ERR_IO;
    EXPECT_EQ(el_log_deferred_sink_drain(&sink, recording_sink_as_sink(&recording)), 2u);
    EXPECT_EQ(recording.calls, 2u);
    EXPECT_EQ(std::string(recording.last.message, recording.last.len), "two");
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 0u);
}

TEST_F(ElLogDeferredSink, AMessageLongerThanTheDestinationsBufferIsGivenCut)
{
    ASSERT_EQ(Write("abcdefghijklmnopqrstuvwxyz0123456789"), EL_OK);
    ASSERT_EQ(Write("next"), EL_OK);

    recording_sink_set_capacity(&recording, 10);
    EXPECT_EQ(el_log_deferred_sink_drain(&sink, recording_sink_as_sink(&recording)), 2u);
    ASSERT_EQ(recording.calls, 2u);
    /* The second record, whole: the rest of the first was dropped. */
    EXPECT_EQ(std::string(recording.last.message, recording.last.len), "next");
    EXPECT_EQ(recording.last_fit, EL_OK);

    ASSERT_EQ(Write("abcdefghijklmnopqrstuvwxyz0123456789"), EL_OK);
    EXPECT_EQ(el_log_deferred_sink_drain(&sink, recording_sink_as_sink(&recording)), 1u);
    EXPECT_EQ(std::string(recording.last.message, recording.last.len), "abcdefghij");
    EXPECT_EQ(recording.last_fit, EL_ERR_NO_SPACE) << "a cut message was given as whole";
}

TEST_F(ElLogDeferredSink, AMessageIsReadIntoTheBufferTheDestinationLends)
{
    ASSERT_EQ(Write("in place"), EL_OK);
    EXPECT_EQ(el_log_deferred_sink_drain(&sink, recording_sink_as_sink(&recording)), 1u);
    EXPECT_EQ(recording.last_message_pointer, recording.storage);
    /* What begin saw is the record without its message. */
    EXPECT_EQ(recording.begun.message, nullptr);
    EXPECT_EQ(recording.begun.len, 0u);
    EXPECT_EQ(recording.begun.timestamp, 5u);
}

/* The destination logs through the same deferred sink while it is drained
 * into: what it logs waits for the next drain, so a drain ends. */
TEST_F(ElLogDeferredSink, ADrainTakesOnlyWhatWasInTheQueueWhenItBegan)
{
    const el_log_record again = Record("echo");
    echoing_sink echoing;
    echoing_sink_init(&echoing, handle, &again, 10);
    ASSERT_EQ(Write("one"), EL_OK);
    ASSERT_EQ(Write("two"), EL_OK);

    EXPECT_EQ(el_log_deferred_sink_drain(&sink, echoing_sink_as_sink(&echoing)), 2u);
    EXPECT_EQ(echoing.ends, 2u);
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), 2 * el_log_deferred_sink_record_size(4));

    EXPECT_EQ(Drain(), 2u);
    EXPECT_EQ(KeptMessage(0), "echo");
    EXPECT_EQ(KeptMessage(1), "echo");
}

TEST_F(ElLogDeferredSink, APrintfMessageIsFormattedInTheRecordArea)
{
    el_manual_clock manual;
    el_manual_clock_init(&manual, 1000);
    el_manual_clock_advance(&manual, 1750);
    el_logger log;
    el_logger_init(&log, handle, el_manual_clock_as_clock(&manual), "uart", EL_LOG_LEVEL_INFO);

    EL_LOG_INFO(&log, "received %u bytes, %s", 42u, "then stopped");
    EL_LOG_DEBUG(&log, "below the logger's level");
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), el_log_deferred_sink_record_size(31));

    ASSERT_EQ(Drain(), 1u);
    el_log_record back;
    el_log_mem_sink_get(&kept, 0, &back);
    EXPECT_EQ(std::string(back.message, back.len), "received 42 bytes, then stopped");
    EXPECT_EQ(back.timestamp, 1750u);
    EXPECT_STREQ(back.tag, "uart");
}

TEST_F(ElLogDeferredSink, APrintfMessageThatDoesNotFitTheRecordAreaIsLostWhole)
{
    Open(kQueueMax, 10);
    el_manual_clock manual;
    el_manual_clock_init(&manual, 1000);
    el_logger log;
    el_logger_init(&log, handle, el_manual_clock_as_clock(&manual), "uart", EL_LOG_LEVEL_INFO);

    EL_LOG_INFO(&log, "%s", "eleven byte");
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 1u);
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), 0u);

    EL_LOG_INFO(&log, "%s", "ten bytes.");
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 1u);
    EXPECT_EQ(Drain(), 1u);
    EXPECT_EQ(KeptMessage(0), "ten bytes.");
}

/* Entries are copied in and out: the storage needs no alignment. */
TEST_F(ElLogDeferredSink, WorksInStorageOfAnyAlignment)
{
    el_log_deferred_sink_init(&sink, storage + 1, sizeof(storage) - 1, kMessageMax);
    handle = el_log_deferred_sink_as_sink(&sink);

    for (unsigned i = 0; i < 40; i++) {
        SCOPED_TRACE(testing::Message() << "record " << i);
        const std::string message(i % 7, static_cast<char>('a' + i % 26));
        const el_log_record record = Record(message.c_str(), 0x0102030405060708ULL + i);
        ASSERT_EQ(el_log_sink_write(handle, &record), EL_OK);
        ASSERT_EQ(el_log_deferred_sink_drain(&sink, recording_sink_as_sink(&recording)), 1u);
        EXPECT_EQ(recording.last.timestamp, 0x0102030405060708ULL + i);
        EXPECT_EQ(std::string(recording.last.message, recording.last.len), message);
    }
}

/* What EL_LOG_DEFERRED_SINK_STORAGE_SIZE promises: one record's bytes for the
 * record area, and for each record that may wait. */
TEST_F(ElLogDeferredSink, StorageSizeIsTheFunctionsRecordSizeForTheAreaAndEveryWaitingRecord)
{
    const size_t waiting[] = {0, 1, 3, 8};
    const size_t longest[] = {0, 1, 24, 64};
    for (const size_t n : waiting) {
        for (const size_t m : longest) {
            EXPECT_EQ(EL_LOG_DEFERRED_SINK_STORAGE_SIZE(n, m),
                      (n + 1) * el_log_deferred_sink_record_size(m))
                << n << " records of " << m << " bytes";
        }
    }

    /* A constant expression, for a static array. */
    static uint8_t constant[EL_LOG_DEFERRED_SINK_STORAGE_SIZE(3, 10)];
    EXPECT_EQ(sizeof(constant), 4 * el_log_deferred_sink_record_size(10));
}

TEST_F(ElLogDeferredSink, StorageOfTheMacrosSizeHoldsNRecordsOfMBytesAndLosesTheNext)
{
    constexpr size_t kWaiting = 4;
    constexpr size_t kLongest = 9;
    static uint8_t exact[EL_LOG_DEFERRED_SINK_STORAGE_SIZE(kWaiting, kLongest)];
    el_log_deferred_sink_init(&sink, exact, sizeof(exact), kLongest);
    handle = el_log_deferred_sink_as_sink(&sink);
    const size_t one = el_log_deferred_sink_record_size(kLongest);

    for (size_t i = 1; i <= kWaiting; i++) {
        EXPECT_EQ(Write("123456789"), EL_OK) << "record " << i;
        EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), i * one);
    }
    EXPECT_EQ(Write("123456789"), EL_ERR_NO_SPACE) << "the next one";
    EXPECT_EQ(el_log_deferred_sink_lost(&sink), 1u);
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), kWaiting * one);

    /* The record area is as long as the longest message, and no longer. */
    EXPECT_EQ(Drain(), kWaiting);
    EXPECT_EQ(Write("1234567890"), EL_ERR_NO_SPACE);
    EXPECT_EQ(Write("123456789"), EL_OK);
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), one);
}

TEST_F(ElLogDeferredSink, PendingBytesAreTheRecordsThatWaitAndFollowTheDrain)
{
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), 0u);
    ASSERT_EQ(Write("a"), EL_OK);
    ASSERT_EQ(Write("bcd"), EL_OK);
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink),
              el_log_deferred_sink_record_size(1) + el_log_deferred_sink_record_size(3));

    EXPECT_EQ(Drain(), 2u);
    EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), 0u);
}

} // namespace
