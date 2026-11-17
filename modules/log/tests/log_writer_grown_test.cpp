/*
 * The writer sink over grown storage (elemental/log_writer.h): a line is as
 * long as the allocator allows, and only an allocator that refuses cuts it.
 * What the sink does over fixed storage is in log_sinks_test.cpp.
 */
#include <gtest/gtest.h>

#include <elemental/alloc_arena.h>
#include <elemental/io_alloc.h>
#include <elemental/io_buf.h>
#include <elemental/log.h>
#include <elemental/log_printf.h>
#include <elemental/log_writer.h>
#include <elemental/time_manual.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace
{

/* Static, not in the fixture: a Zephyr image runs the tests on a 4 KiB
 * stack and a small heap. */
alignas(16) uint8_t arena_storage[8192];
char out_storage[2048];
char message[1001];

class ElLogWriterSinkGrown : public testing::Test
{
  protected:
    void SetUp() override
    {
        std::memset(message, 'x', sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
    }

    /* A sink into an el_buf of 2 KiB, its line grown from an arena of
     * `arena_cap` bytes. */
    void Open(size_t arena_cap)
    {
        el_buf_init(&out, out_storage, sizeof(out_storage));
        el_bump_arena_init(&arena, arena_storage, arena_cap);
        line = el_grow_buf_init(&grown, el_bump_arena_as_allocator(&arena));
        el_log_writer_sink_init(&sink, el_buf_as_writer(&out), line);
        handle = el_log_writer_sink_as_sink(&sink);
    }

    void TearDown() override
    {
        el_grow_buf_deinit(&grown);
    }

    el_status Write(const char *text)
    {
        el_log_record record;
        record.timestamp = 1;
        record.ticks_per_second = 1;
        record.level = EL_LOG_LEVEL_WARN;
        record.tag = "t";
        record.message = text;
        record.len = std::strlen(text);
        return el_log_sink_write(handle, &record);
    }

    std::string Written() const
    {
        return std::string(reinterpret_cast<const char *>(el_buf_data(&out)), el_buf_len(&out));
    }

    el_buf out;
    el_bump_arena arena;
    el_grow_buf grown;
    el_buf *line = nullptr;
    el_log_writer_sink sink;
    el_log_sink handle;
};

} // namespace

TEST_F(ElLogWriterSinkGrown, ALongLineGoesOutWhole)
{
    Open(sizeof(arena_storage));
    const std::string whole = std::string("[     1.000000] W t: ") + message + "\n";

    EXPECT_EQ(Write(message), EL_OK);
    EXPECT_EQ(Written(), whole);
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 0u);

    /* The buffer kept what it grew to: a short line after it is whole too. */
    EXPECT_GE(el_buf_room(line) + el_buf_len(line), whole.size());
    EXPECT_EQ(Write("short"), EL_OK);
    EXPECT_EQ(Written(), whole + "[     1.000000] W t: short\n");
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 0u);
}

TEST_F(ElLogWriterSinkGrown, ALongPrintfMessageGoesOutWhole)
{
    /* Formatted straight into the grown line: no limit but the allocator. */
    Open(sizeof(arena_storage));
    el_manual_clock clock;
    el_manual_clock_init(&clock, 1);
    el_manual_clock_advance(&clock, 1);
    el_logger log;
    el_logger_init(&log, handle, el_manual_clock_as_clock(&clock), "t", EL_LOG_LEVEL_DEBUG);

    EL_LOG_WARN(&log, "%s|%d", message, 1234567);
    EXPECT_EQ(Written(), std::string("[     1.000000] W t: ") + message + "|1234567\n");
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 0u);
}

TEST_F(ElLogWriterSinkGrown, WhenTheAllocatorRefusesTheLineIsCutAndEndsInANewline)
{
    Open(256);
    const std::string whole = std::string("[     1.000000] W t: ") + message + "\n";

    EXPECT_EQ(Write(message), EL_ERR_NO_SPACE);
    const std::string written = Written();
    ASSERT_FALSE(written.empty());
    EXPECT_LT(written.size(), whole.size());
    EXPECT_EQ(written.back(), '\n');
    /* As much of the line as the buffer took, then the newline. */
    EXPECT_EQ(written.substr(0, written.size() - 1), whole.substr(0, written.size() - 1));
    EXPECT_GE(written.size(), std::strlen("[     1.000000] W t: x\n"))
        << "the prefix and some of the message went out";
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 1u);

    /* A line the allocator allows still goes out whole. */
    EXPECT_EQ(Write("short"), EL_OK);
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 1u);
}

TEST_F(ElLogWriterSinkGrown, WhenTheAllocatorGivesNothingEveryRecordIsLostAndNothingIsWritten)
{
    Open(0);

    EXPECT_EQ(Write("m"), EL_ERR_NO_SPACE);
    EXPECT_EQ(Write(""), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_buf_len(&out), 0u);
    EXPECT_EQ(el_log_writer_sink_lost(&sink), 2u);
}
