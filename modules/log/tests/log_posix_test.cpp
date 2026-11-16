/*
 * Two sinks in one program: records kept in memory, and lines on a POSIX
 * descriptor through the writer sink, timed by the system's clock. Host
 * only: it needs io_posix and time_posix.
 */
#include <gtest/gtest.h>

#include <elemental/io_posix.h>
#include <elemental/log.h>
#include <elemental/log_mem.h>
#include <elemental/log_printf.h>
#include <elemental/log_writer.h>
#include <elemental/time_posix.h>

#include <cstdint>
#include <string>

#include <signal.h>
#include <unistd.h>

namespace
{

class ElLogPosix : public testing::Test
{
  protected:
    void SetUp() override
    {
        ASSERT_EQ(pipe(ends), 0);
        el_posix_fd_init(&out, ends[1]);
        el_log_writer_sink_init(&shown, el_posix_fd_as_writer(&out), line, sizeof(line));
        el_log_mem_sink_init(&kept, storage, sizeof(storage));
        el_logger_init(&to_descriptor, el_log_writer_sink_as_sink(&shown), el_posix_clock(), "fd",
                       EL_LOG_LEVEL_INFO);
        el_logger_init(&to_memory, el_log_mem_sink_as_sink(&kept), el_posix_clock(), "mem",
                       EL_LOG_LEVEL_DEBUG);
    }

    void TearDown() override
    {
        for (const int end : ends) {
            if (end >= 0) {
                close(end);
            }
        }
    }

    /* Everything written to the pipe so far. */
    std::string Piped()
    {
        close(ends[1]);
        ends[1] = -1;
        std::string text;
        char chunk[256];
        ssize_t got;
        while ((got = read(ends[0], chunk, sizeof(chunk))) > 0) {
            text.append(chunk, static_cast<size_t>(got));
        }
        return text;
    }

    int ends[2] = {-1, -1};
    el_posix_fd out;
    char line[160];
    el_log_writer_sink shown;
    uint8_t storage[2048];
    el_log_mem_sink kept;
    el_logger to_descriptor;
    el_logger to_memory;
};

} // namespace

TEST_F(ElLogPosix, AMemorySinkAndADescriptorCoexistAndEachGetsItsLoggersRecords)
{
    EL_LOG_INFO(&to_descriptor, "listening on port %d", 8080);
    EL_LOG_DEBUG(&to_memory, "only in memory");
    EL_LOG_DEBUG(&to_descriptor, "below this logger's level");
    EL_LOG_ERROR(&to_memory, "disk %s", "full");
    EL_LOG_WARN(&to_descriptor, "second line");

    /* Memory: two records, as records. */
    ASSERT_EQ(el_log_mem_sink_count(&kept), 2u);
    el_log_record record;
    el_log_mem_sink_get(&kept, 1, &record);
    EXPECT_EQ(record.level, EL_LOG_LEVEL_ERROR);
    EXPECT_STREQ(record.tag, "mem");
    EXPECT_EQ(std::string(record.message, record.len), "disk full");
    EXPECT_EQ(record.ticks_per_second, 1000000000u);

    /* The descriptor: two lines, as text, each after its bracketed time. */
    const std::string text = Piped();
    const size_t first_end = text.find('\n');
    ASSERT_NE(first_end, std::string::npos);
    const std::string first = text.substr(0, first_end);
    const std::string second = text.substr(first_end + 1);
    EXPECT_EQ(first.front(), '[');
    EXPECT_EQ(first.substr(first.find("] ")), "] I fd: listening on port 8080");
    EXPECT_EQ(second.substr(second.find("] ")), "] W fd: second line\n");
    EXPECT_EQ(el_log_writer_sink_lost(&shown), 0u);
}

TEST_F(ElLogPosix, RecordsAreStampedInOrderByTheSystemsClock)
{
    for (int i = 0; i < 20; i++) {
        EL_LOG_INFO(&to_memory, "%d", i);
    }

    ASSERT_EQ(el_log_mem_sink_count(&kept), 20u);
    el_ticks last = 0;
    for (size_t i = 0; i < 20; i++) {
        el_log_record record;
        el_log_mem_sink_get(&kept, i, &record);
        EXPECT_GE(record.timestamp, last) << "record " << i;
        last = record.timestamp;
    }
    EXPECT_GT(last, 0u);
}

TEST_F(ElLogPosix, ADescriptorWhoseReaderIsGoneLosesRecordsAndSaysHowMany)
{
    /* So that the closed pipe is reported as a status, not a signal. */
    signal(SIGPIPE, SIG_IGN);
    close(ends[0]);
    ends[0] = -1;

    EL_LOG_ERROR(&to_descriptor, "nobody is reading");
    EL_LOG_ERROR(&to_descriptor, "still nobody");

    EXPECT_EQ(el_log_writer_sink_lost(&shown), 2u);
}
