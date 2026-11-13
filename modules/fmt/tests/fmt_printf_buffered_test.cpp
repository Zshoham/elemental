/*
 * printf into a buffered writer (el_fmt_printf_buffered): it streams, it
 * stops at an item when the stream stalls, and the examples of
 * docs/proposals/io-buffers-fmt.md behave as stated. The stream is a
 * scripted stand-in for a UART: a FIFO the test drains, taking a set number
 * of bytes per call and reporting EL_ERR_WOULD_BLOCK when it is full. These
 * run in the Zephyr test image too, so Example 2 runs on each target board
 * against a writer that stalls.
 */
#include <gtest/gtest.h>

#include <elemental/fmt_printf.h>
#include <elemental/io_buffered.h>

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <string>

namespace
{

/* A transmit FIFO: takes at most `per_call` bytes per call and at most
 * `room` in all until drained; never waits. */
class Fifo
{
  public:
    el_writer writer()
    {
        static const el_writer_ops ops = {Write};
        return el_writer{this, &ops};
    }

    std::string sent;
    size_t per_call = 1000;
    size_t room = 0;
    el_status fail = EL_OK;

  private:
    static el_status Write(void *ctx, const void *data, size_t len, size_t *n)
    {
        Fifo *const self = static_cast<Fifo *>(ctx);
        if (self->fail != EL_OK) {
            return self->fail;
        }
        if (self->room == 0) {
            return EL_ERR_WOULD_BLOCK;
        }
        size_t count = len < self->per_call ? len : self->per_call;
        count = count < self->room ? count : self->room;
        self->sent.append(static_cast<const char *>(data), count);
        self->room -= count;
        *n = count;
        return EL_OK;
    }
};

std::string Pending(const el_buf *b, const el_buffered_writer *w)
{
    const size_t pending = el_buffered_writer_pending(w);
    const uint8_t *const data = el_buf_data(b);
    return std::string(reinterpret_cast<const char *>(data) + el_buf_len(b) - pending, pending);
}

class FmtBuffered : public testing::Test
{
  protected:
    void Open(size_t cap)
    {
        el_buf_init(&buf, storage, cap);
        el_buffered_writer_init(&w, fifo.writer(), &buf);
    }

    Fifo fifo;
    uint8_t storage[256];
    el_buf buf;
    el_buffered_writer w;
};

TEST_F(FmtBuffered, OutputLongerThanTheBufferStreamsThrough)
{
    Open(8);
    fifo.room = 1000;
    fifo.per_call = 5;
    const std::string text(40, 's');
    EXPECT_EQ(el_fmt_printf_buffered(&w, "<%s|%08x|%d>", text.c_str(), 0xbeefu, -12345), EL_OK);
    EXPECT_EQ(el_buffered_writer_flush(&w), EL_OK);
    EXPECT_EQ(fifo.sent, "<" + text + "|0000beef|-12345>");
}

TEST_F(FmtBuffered, AStallStopsAtAnItemAndTheNumberIsAbsentNotCut)
{
    /* The proposal's measurement: an 8-byte buffer in front of a stream
     * that takes 3 bytes per call and stalls after 10. */
    Open(8);
    fifo.per_call = 3;
    fifo.room = 10;
    EXPECT_EQ(el_fmt_printf_buffered(&w, "t=%u h=%u p=%u\n", 1234567u, 89u, 1013u),
              EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(fifo.sent, "t=1234567 ");
    EXPECT_EQ(Pending(&buf, &w), "h=89 p=");

    /* Resuming means resuming the flush. */
    fifo.room = 1000;
    EXPECT_EQ(el_buffered_writer_flush(&w), EL_OK);
    EXPECT_EQ(fifo.sent, "t=1234567 h=89 p=");
}

TEST_F(FmtBuffered, ANumberLargerThanFixedStorageIsNoSpace)
{
    Open(4);
    fifo.room = 1000;
    EXPECT_EQ(el_fmt_printf_buffered(&w, "ab%5d", 1), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_buffered_writer_flush(&w), EL_OK);
    EXPECT_EQ(fifo.sent, "ab");
}

TEST_F(FmtBuffered, CapacityZeroAcceptsNoItem)
{
    Open(0);
    fifo.room = 1000;
    EXPECT_EQ(el_fmt_printf_buffered(&w, "x"), EL_ERR_NO_SPACE);
    EXPECT_EQ(el_fmt_printf_buffered(&w, "%d", 1), EL_ERR_NO_SPACE);
    EXPECT_EQ(fifo.sent, "");
}

TEST_F(FmtBuffered, AnEndedStreamTakesNothingEvenWhatWouldFit)
{
    Open(16);
    fifo.fail = EL_ERR_END_OF_STREAM;
    ASSERT_EQ(el_fmt_printf_buffered(&w, "abc"), EL_OK);
    ASSERT_EQ(el_buffered_writer_flush(&w), EL_ERR_END_OF_STREAM);
    EXPECT_EQ(el_fmt_printf_buffered(&w, "d"), EL_ERR_END_OF_STREAM);
}

TEST_F(FmtBuffered, TheStreamsFailureIsReturned)
{
    Open(4);
    fifo.fail = EL_ERR_IO;
    EXPECT_EQ(el_fmt_printf_buffered(&w, "abcdef"), EL_ERR_IO);
    fifo.fail = EL_OK;
    fifo.room = 1000;
    EXPECT_EQ(el_buffered_writer_flush(&w), EL_OK);
    EXPECT_EQ(fifo.sent, "abcd");
}

/* ---- Example 2: printf to a non-blocking UART ------------------------------ */

constexpr size_t kLineMax = 64; /* the longest report line */

struct Console {
    Fifo uart;
    uint8_t storage[256];
    el_buf buf;
    el_buffered_writer out;
    uint32_t dropped = 0;

    Console()
    {
        el_buf_init(&buf, storage, sizeof storage);
        el_buffered_writer_init(&out, uart.writer(), &buf);
    }

    /* The proposal's report(), unchanged but for the names. */
    void Report(uint32_t uptime_s, const char *name, size_t name_len, int32_t temp_mc)
    {
        if (el_buffered_writer_reserve(&out, kLineMax) != EL_OK) {
            dropped++;
            return;
        }
        const size_t mark = el_buf_len(&buf);
        if (el_fmt_printf_buffered(&out, "%8" PRIu32 " %.*s %" PRId32 " mC\n", uptime_s,
                                   static_cast<int>(name_len), name, temp_mc) != EL_OK) {
            if (el_buf_len(&buf) - el_buffered_writer_pending(&out) <= mark) {
                el_buf_truncate(&buf, mark);
            }
            dropped++;
        }
        (void)el_buffered_writer_flush(&out);
    }
};

/* The line report() writes, built without printf. */
std::string Line(uint32_t uptime_s, const char *name, int32_t temp_mc)
{
    std::string uptime = std::to_string(uptime_s);
    uptime.insert(0, uptime.size() < 8 ? 8 - uptime.size() : 0, ' ');
    return uptime + " " + name + " " + std::to_string(temp_mc) + " mC\n";
}

TEST(FmtExample2, ReportsWaitInTheBufferWhileTheUartIsStalledAndLeaveWhole)
{
    Console console;
    std::string expected;
    for (uint32_t i = 0; i < 6; i++) {
        console.Report(100 + i, "board", 5, -12500 + static_cast<int32_t>(i));
        expected += Line(100 + i, "board", -12500 + static_cast<int32_t>(i));
    }
    EXPECT_EQ(console.dropped, 0u);
    EXPECT_EQ(console.uart.sent, "");
    EXPECT_EQ(el_buffered_writer_pending(&console.out), expected.size());
    EXPECT_EQ(expected.size(), 150u);

    /* The FIFO drains 16 bytes at a time; console_poll on each event. */
    console.uart.per_call = 16;
    for (int event = 0; event < 20 && el_buffered_writer_pending(&console.out) != 0; event++) {
        console.uart.room = 16;
        (void)el_buffered_writer_flush(&console.out);
    }
    EXPECT_EQ(console.uart.sent, expected);
}

TEST(FmtExample2, AReportWithNoRoomForAWholeLineIsDroppedWhole)
{
    Console console;
    std::string expected;
    for (uint32_t i = 0; i < 10; i++) {
        console.Report(i, "board", 5, -21000);
        if (i < 8) {
            expected += Line(i, "board", -21000);
        }
    }
    /* 8 lines of 25 bytes leave 56, less than a whole line: 2 dropped. */
    EXPECT_EQ(console.dropped, 2u);
    console.uart.room = 1000;
    EXPECT_EQ(el_buffered_writer_flush(&console.out), EL_OK);
    EXPECT_EQ(console.uart.sent, expected);
}

TEST(FmtExample2, AnOverlongLineIsTakenBackWhileNoneOfItWasPassedOn)
{
    Console console;
    console.Report(1, "board", 5, 0);
    /* Longer than the room left: it stalls part way, before any of it was
     * passed on, and nothing was compacted since the mark. */
    const std::string name(240, 'n');
    console.Report(2, name.data(), name.size(), 0);
    EXPECT_EQ(console.dropped, 1u);
    console.uart.room = 1000;
    EXPECT_EQ(el_buffered_writer_flush(&console.out), EL_OK);
    EXPECT_EQ(console.uart.sent, Line(1, "board", 0));
}

TEST(FmtExample2, LinesLeaveWholeWhenTheUartTakesSomeOfEachReport)
{
    Console console;
    std::string expected;
    for (uint32_t i = 0; i < 40; i++) {
        console.uart.room = 7; /* the FIFO has room for 7 bytes per report */
        console.Report(i, "sensor", 6, static_cast<int32_t>(i) * 1000);
        expected += Line(i, "sensor", static_cast<int32_t>(i) * 1000);
    }
    console.uart.room = 10000;
    EXPECT_EQ(el_buffered_writer_flush(&console.out), EL_OK);
    /* Whatever was dropped was dropped whole: what arrived is a sequence of
     * complete lines, each one of the expected lines, in order. */
    size_t at = 0;
    for (uint32_t i = 0; i < 40 && at < console.uart.sent.size(); i++) {
        const std::string line = Line(i, "sensor", static_cast<int32_t>(i) * 1000);
        if (console.uart.sent.compare(at, line.size(), line) == 0) {
            at += line.size();
        }
    }
    EXPECT_EQ(at, console.uart.sent.size()) << console.uart.sent;
    EXPECT_GT(console.dropped, 0u);
    EXPECT_LT(console.dropped, 40u);
}

} // namespace
