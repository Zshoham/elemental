/* What only the tee sink promises (tee_sink.h). */
#include <gtest/gtest.h>

#include <elemental/log_mem.h>

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
        tee_sink_init(&tee, el_log_mem_sink_as_sink(&first), el_log_mem_sink_as_sink(&second));
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

    tee_sink_init(&tee, io, ended);
    EXPECT_EQ(el_log_sink_write(tee_sink_as_sink(&tee), &record), EL_ERR_IO);
    tee_sink_init(&tee, ended, io);
    EXPECT_EQ(el_log_sink_write(tee_sink_as_sink(&tee), &record), EL_ERR_END_OF_STREAM);
}

TEST_F(TeeSink, ATeeOfATeeReachesThreeSinks)
{
    uint8_t third_storage[128];
    el_log_mem_sink third;
    el_log_mem_sink_init(&third, third_storage, sizeof(third_storage));
    tee_sink outer;
    tee_sink_init(&outer, sink, el_log_mem_sink_as_sink(&third));

    EXPECT_EQ(el_log_sink_write(tee_sink_as_sink(&outer), &record), EL_OK);
    EXPECT_EQ(el_log_mem_sink_count(&first), 1u);
    EXPECT_EQ(el_log_mem_sink_count(&second), 1u);
    EXPECT_EQ(el_log_mem_sink_count(&third), 1u);
}
