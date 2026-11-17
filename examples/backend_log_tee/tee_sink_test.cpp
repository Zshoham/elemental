/* What only the tee sink promises (tee_sink.h). */
#include <gtest/gtest.h>

#include <elemental/io_buf.h>
#include <elemental/log_mem.h>
#include <elemental/log_printf.h>
#include <elemental/time_manual.h>

#include <cstdint>
#include <string>

#include "refusing_sink.h"
#include "tee_sink.h"

namespace
{

class TeeSink : public testing::Test
{
  protected:
    void SetUp() override
    {
        Open(sizeof(first_storage), sizeof(second_storage));
        record.timestamp = 1;
        record.ticks_per_second = 1000;
        record.level = EL_LOG_LEVEL_WARN;
        record.tag = "tee";
        record.message = "to both";
        record.len = 7;
    }

    void Open(size_t first_cap, size_t second_cap)
    {
        el_log_mem_sink_init(&first, first_storage, first_cap);
        el_log_mem_sink_init(&second, second_storage, second_cap);
        el_buf_init(&message, message_storage, sizeof(message_storage));
        tee_sink_init(&tee, el_log_mem_sink_as_sink(&first), el_log_mem_sink_as_sink(&second),
                      &message);
        sink = tee_sink_as_sink(&tee);
    }

    static std::string Only(const el_log_mem_sink &from)
    {
        el_log_record kept;
        el_log_mem_sink_get(&from, 0, &kept);
        return std::string(kept.message, kept.len);
    }

    uint8_t first_storage[128];
    uint8_t second_storage[128];
    char message_storage[32];
    el_buf message;
    el_log_mem_sink first;
    el_log_mem_sink second;
    tee_sink tee;
    el_log_sink sink;
    el_log_record record;
};

} // namespace

TEST_F(TeeSink, GivesEachRecordToBothSinks)
{
    EXPECT_EQ(el_log_sink_write(sink, &record), EL_OK);

    ASSERT_EQ(el_log_mem_sink_count(&first), 1u);
    ASSERT_EQ(el_log_mem_sink_count(&second), 1u);
    EXPECT_EQ(Only(first), "to both");
    EXPECT_EQ(Only(second), "to both");
}

TEST_F(TeeSink, TheSecondSinkGetsTheRecordEvenWhenTheFirstRefusesIt)
{
    Open(0, sizeof(second_storage));

    EXPECT_EQ(el_log_sink_write(sink, &record), EL_ERR_NO_SPACE) << "the first sink's status";
    EXPECT_EQ(el_log_mem_sink_count(&first), 0u);
    ASSERT_EQ(el_log_mem_sink_count(&second), 1u);
    EXPECT_EQ(Only(second), "to both");
}

TEST_F(TeeSink, ReturnsTheSecondSinksStatusWhenOnlyItFails)
{
    Open(sizeof(first_storage), 0);

    EXPECT_EQ(el_log_sink_write(sink, &record), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_log_mem_sink_count(&first), 1u);
    EXPECT_EQ(el_log_mem_sink_lost(&second), 1u);
}

TEST_F(TeeSink, WhenBothFailTheFirstSinksStatusComesBack)
{
    const el_log_sink io = refusing_sink(EL_ERR_IO);
    const el_log_sink ended = refusing_sink(EL_ERR_END_OF_STREAM);

    tee_sink_init(&tee, io, ended, &message);
    EXPECT_EQ(el_log_sink_write(tee_sink_as_sink(&tee), &record), EL_ERR_IO);
    tee_sink_init(&tee, ended, io, &message);
    EXPECT_EQ(el_log_sink_write(tee_sink_as_sink(&tee), &record), EL_ERR_END_OF_STREAM);
}

TEST_F(TeeSink, ATeeOfATeeReachesThreeSinks)
{
    uint8_t third_storage[128];
    el_log_mem_sink third;
    el_log_mem_sink_init(&third, third_storage, sizeof(third_storage));
    char outer_storage[32];
    el_buf outer_message;
    el_buf_init(&outer_message, outer_storage, sizeof(outer_storage));
    tee_sink outer;
    tee_sink_init(&outer, sink, el_log_mem_sink_as_sink(&third), &outer_message);

    EXPECT_EQ(el_log_sink_write(tee_sink_as_sink(&outer), &record), EL_OK);
    EXPECT_EQ(el_log_mem_sink_count(&first), 1u);
    EXPECT_EQ(el_log_mem_sink_count(&second), 1u);
    EXPECT_EQ(el_log_mem_sink_count(&third), 1u);
}

TEST_F(TeeSink, AMessageItsBufferCannotTakeReachesBothCutAndIsReported)
{
    el_buf_init(&message, message_storage, 4);

    EXPECT_EQ(el_log_sink_write(sink, &record), EL_ERR_NO_SPACE);
    EXPECT_EQ(Only(first), "to b");
    EXPECT_EQ(Only(second), "to b");
}

TEST_F(TeeSink, APrintfMessageIsFormattedOnceAndReachesBoth)
{
    el_manual_clock clock;
    el_manual_clock_init(&clock, 1000);
    el_logger log;
    el_logger_init(&log, sink, el_manual_clock_as_clock(&clock), "tee", EL_LOG_LEVEL_DEBUG);

    EL_LOG_INFO(&log, "%d and %d", 1, 2);
    EXPECT_EQ(Only(first), "1 and 2");
    EXPECT_EQ(Only(second), "1 and 2");
}
