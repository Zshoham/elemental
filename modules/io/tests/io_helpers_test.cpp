/*
 * el_read(), el_write(), el_read_exact() and el_write_all(), driven by a
 * scripted stream: each call to the stream plays the next step of a fixed
 * script. No device, no timing, and every interleaving of progress,
 * would-block, and failure is reproducible.
 */
#include <gtest/gtest.h>

#include <elemental/io.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace
{

constexpr size_t kUnset = static_cast<size_t>(-1);

struct Step {
    el_status status;
    size_t count; /* bytes to move when status is EL_OK */
};

/* A stream that plays a script. One object serves as reader or writer. */
class Script
{
  public:
    Script(std::initializer_list<Step> steps) : steps_(steps)
    {
    }

    el_reader reader()
    {
        static const el_reader_ops ops = {Read};
        return el_reader{this, &ops};
    }

    el_writer writer()
    {
        static const el_writer_ops ops = {Write};
        return el_writer{this, &ops};
    }

    /* How many times the stream was called. */
    size_t calls = 0;
    /* The largest request the stream was ever asked for. */
    size_t largest_request = 0;
    /* What a writer was given, in order. */
    std::string written;

  private:
    Step Next(size_t request)
    {
        EXPECT_LT(calls, steps_.size()) << "the stream was called more often than scripted";
        largest_request = std::max(largest_request, request);
        return steps_[calls++ % steps_.size()];
    }

    /* A reader produces the byte values 0, 1, 2, ... */
    static el_status Read(void *ctx, void *buf, size_t cap, size_t *n)
    {
        Script *const self = static_cast<Script *>(ctx);
        const Step step = self->Next(cap);
        if (step.status != EL_OK) {
            return step.status;
        }
        const size_t count = std::min(step.count, cap);
        uint8_t *const bytes = static_cast<uint8_t *>(buf);
        for (size_t i = 0; i < count; i++) {
            bytes[i] = self->next_byte_++;
        }
        *n = count;
        return EL_OK;
    }

    static el_status Write(void *ctx, const void *data, size_t len, size_t *n)
    {
        Script *const self = static_cast<Script *>(ctx);
        const Step step = self->Next(len);
        if (step.status != EL_OK) {
            return step.status;
        }
        const size_t count = std::min(step.count, len);
        self->written.append(static_cast<const char *>(data), count);
        *n = count;
        return EL_OK;
    }

    std::vector<Step> steps_;
    uint8_t next_byte_ = 0;
};

std::vector<uint8_t> Bytes(const uint8_t *data, size_t len)
{
    return std::vector<uint8_t>(data, data + len);
}

/* The bytes a scripted reader produces: `len` values counting up from `first`. */
std::vector<uint8_t> Counting(uint8_t first, size_t len)
{
    std::vector<uint8_t> bytes(len);
    for (size_t i = 0; i < len; i++) {
        bytes[i] = static_cast<uint8_t>(first + i);
    }
    return bytes;
}

/* ---- Single attempts ---------------------------------------------------- */

TEST(IoSingleAttempt, ReadMakesExactlyOneAttempt)
{
    Script s{{EL_OK, 3}, {EL_OK, 3}};
    uint8_t buf[8];
    size_t n = kUnset;

    /* A short read is success, and the stream is not asked again. */
    EXPECT_EQ(el_read(s.reader(), buf, sizeof(buf), &n), EL_OK);
    EXPECT_EQ(n, 3u);
    EXPECT_EQ(s.calls, 1u);
    EXPECT_EQ(Bytes(buf, 3), Counting(0, 3));
}

TEST(IoSingleAttempt, ReadPassesStatusesThroughWithZeroProgress)
{
    for (const el_status status : {EL_ERR_WOULD_BLOCK, EL_ERR_END_OF_STREAM, EL_ERR_IO}) {
        Script s{{status, 0}};
        uint8_t buf[4];
        size_t n = kUnset;
        EXPECT_EQ(el_read(s.reader(), buf, sizeof(buf), &n), status);
        EXPECT_EQ(n, 0u);
    }
}

TEST(IoSingleAttempt, ZeroSizedRequestsDoNotReachTheStream)
{
    Script s{{EL_ERR_IO, 0}};
    size_t n = kUnset;

    EXPECT_EQ(el_read(s.reader(), nullptr, 0, &n), EL_OK);
    EXPECT_EQ(n, 0u);
    n = kUnset;
    EXPECT_EQ(el_write(s.writer(), nullptr, 0, &n), EL_OK);
    EXPECT_EQ(n, 0u);
    n = kUnset;
    EXPECT_EQ(el_read_exact(s.reader(), nullptr, 0, &n), EL_OK);
    EXPECT_EQ(n, 0u);
    n = kUnset;
    EXPECT_EQ(el_write_all(s.writer(), nullptr, 0, &n), EL_OK);
    EXPECT_EQ(n, 0u);

    EXPECT_EQ(s.calls, 0u);
}

TEST(IoSingleAttempt, WriteMakesExactlyOneAttempt)
{
    Script s{{EL_OK, 2}, {EL_OK, 2}};
    size_t n = kUnset;

    EXPECT_EQ(el_write(s.writer(), "abcdef", 6, &n), EL_OK);
    EXPECT_EQ(n, 2u);
    EXPECT_EQ(s.calls, 1u);
    EXPECT_EQ(s.written, "ab");
}

/* A stream with no state may use a null context. */
el_status ReadZeros(void *ctx, void *buf, size_t cap, size_t *n)
{
    EXPECT_EQ(ctx, nullptr);
    std::fill_n(static_cast<uint8_t *>(buf), cap, uint8_t{0});
    *n = cap;
    return EL_OK;
}

TEST(IoSingleAttempt, NullContextIsAllowed)
{
    static const el_reader_ops zeros_ops = {ReadZeros};
    const el_reader zeros = {nullptr, &zeros_ops};
    uint8_t buf[4] = {1, 2, 3, 4};
    size_t n = kUnset;

    EXPECT_EQ(el_read(zeros, buf, sizeof(buf), &n), EL_OK);
    EXPECT_EQ(n, 4u);
    EXPECT_EQ(Bytes(buf, 4), std::vector<uint8_t>(4, 0));
}

/* ---- el_read_exact ------------------------------------------------------ */

TEST(IoReadExact, JoinsShortReads)
{
    Script s{{EL_OK, 1}, {EL_OK, 4}, {EL_OK, 100}};
    uint8_t buf[8];
    size_t n = kUnset;

    EXPECT_EQ(el_read_exact(s.reader(), buf, sizeof(buf), &n), EL_OK);
    EXPECT_EQ(n, 8u);
    EXPECT_EQ(s.calls, 3u);
    EXPECT_EQ(Bytes(buf, 8), Counting(0, 8));
    /* It never asks for more than is still missing. */
    EXPECT_EQ(s.largest_request, 8u);
}

TEST(IoReadExact, IsResumableAfterWouldBlock)
{
    Script s{{EL_OK, 3}, {EL_ERR_WOULD_BLOCK, 0}, {EL_OK, 5}};
    uint8_t buf[8];
    size_t n = kUnset;

    /* Progress made before the stream stalled is reported with the status. */
    EXPECT_EQ(el_read_exact(s.reader(), buf, sizeof(buf), &n), EL_ERR_WOULD_BLOCK);
    ASSERT_EQ(n, 3u);
    EXPECT_EQ(s.calls, 2u);

    /* The caller waits, then asks for the remainder. */
    size_t more = kUnset;
    EXPECT_EQ(el_read_exact(s.reader(), buf + n, sizeof(buf) - n, &more), EL_OK);
    EXPECT_EQ(more, 5u);
    EXPECT_EQ(Bytes(buf, 8), Counting(0, 8));
}

TEST(IoReadExact, StopsAtTheFirstFailure)
{
    Script s{{EL_OK, 2}, {EL_ERR_IO, 0}, {EL_OK, 6}};
    uint8_t buf[8];
    size_t n = kUnset;

    EXPECT_EQ(el_read_exact(s.reader(), buf, sizeof(buf), &n), EL_ERR_IO);
    EXPECT_EQ(n, 2u);
    /* The stream is not touched again after it reports a failure. */
    EXPECT_EQ(s.calls, 2u);
}

TEST(IoReadExact, ReportsAShortStream)
{
    Script s{{EL_OK, 5}, {EL_ERR_END_OF_STREAM, 0}};
    uint8_t buf[8];
    size_t n = kUnset;

    EXPECT_EQ(el_read_exact(s.reader(), buf, sizeof(buf), &n), EL_ERR_END_OF_STREAM);
    EXPECT_EQ(n, 5u);
}

TEST(IoReadExact, AttemptsAreBoundedByTheLength)
{
    /* The slowest stream that still makes progress: one byte per call. */
    Script s{{EL_OK, 1}, {EL_OK, 1}, {EL_OK, 1}, {EL_OK, 1},
             {EL_OK, 1}, {EL_OK, 1}, {EL_OK, 1}, {EL_OK, 1}};
    uint8_t buf[8];
    size_t n = kUnset;

    EXPECT_EQ(el_read_exact(s.reader(), buf, sizeof(buf), &n), EL_OK);
    EXPECT_EQ(n, 8u);
    EXPECT_EQ(s.calls, 8u);
}

TEST(IoReadExact, TheCountIsOptional)
{
    Script s{{EL_OK, 2}, {EL_OK, 2}, {EL_ERR_END_OF_STREAM, 0}};
    uint8_t buf[4];

    /* Success needs no count: it can only be the length asked for. */
    EXPECT_EQ(el_read_exact(s.reader(), buf, sizeof(buf), nullptr), EL_OK);
    EXPECT_EQ(Bytes(buf, 4), Counting(0, 4));
    /* A failure is still reported without one. */
    EXPECT_EQ(el_read_exact(s.reader(), buf, sizeof(buf), nullptr), EL_ERR_END_OF_STREAM);
}

/* ---- el_write_all ------------------------------------------------------- */

TEST(IoWriteAll, JoinsShortWrites)
{
    Script s{{EL_OK, 1}, {EL_OK, 2}, {EL_OK, 100}};
    size_t n = kUnset;

    EXPECT_EQ(el_write_all(s.writer(), "abcdefgh", 8, &n), EL_OK);
    EXPECT_EQ(n, 8u);
    EXPECT_EQ(s.calls, 3u);
    EXPECT_EQ(s.written, "abcdefgh");
}

TEST(IoWriteAll, IsResumableAfterWouldBlock)
{
    Script s{{EL_OK, 3}, {EL_ERR_WOULD_BLOCK, 0}, {EL_OK, 5}};
    const char *const message = "abcdefgh";
    size_t n = kUnset;

    EXPECT_EQ(el_write_all(s.writer(), message, 8, &n), EL_ERR_WOULD_BLOCK);
    ASSERT_EQ(n, 3u);

    size_t more = kUnset;
    EXPECT_EQ(el_write_all(s.writer(), message + n, 8 - n, &more), EL_OK);
    EXPECT_EQ(more, 5u);
    /* Nothing is written twice and nothing is skipped. */
    EXPECT_EQ(s.written, message);
}

TEST(IoWriteAll, StopsAtTheFirstFailure)
{
    Script s{{EL_OK, 2}, {EL_ERR_IO, 0}, {EL_OK, 6}};
    size_t n = kUnset;

    EXPECT_EQ(el_write_all(s.writer(), "abcdefgh", 8, &n), EL_ERR_IO);
    EXPECT_EQ(n, 2u);
    EXPECT_EQ(s.calls, 2u);
    EXPECT_EQ(s.written, "ab");
}

TEST(IoWriteAll, ReportsAStreamThatEnded)
{
    Script s{{EL_OK, 5}, {EL_ERR_END_OF_STREAM, 0}};
    size_t n = kUnset;

    EXPECT_EQ(el_write_all(s.writer(), "abcdefgh", 8, &n), EL_ERR_END_OF_STREAM);
    EXPECT_EQ(n, 5u);
}

TEST(IoWriteAll, TheCountIsOptional)
{
    Script s{{EL_OK, 1}, {EL_OK, 3}, {EL_ERR_IO, 0}};

    EXPECT_EQ(el_write_all(s.writer(), "abcd", 4, nullptr), EL_OK);
    EXPECT_EQ(s.written, "abcd");
    EXPECT_EQ(el_write_all(s.writer(), "e", 1, nullptr), EL_ERR_IO);
}

/* ---- Application statuses ----------------------------------------------- */

/*
 * A negative status is the application's (elemental/status.h). A stream the
 * application implemented may return one, and the functions in front of it
 * return it unchanged, under the same rules as any other status.
 */
TEST(IoApplicationStatus, AnImplementationsNegativeStatusIsReturnedUnchanged)
{
    constexpr el_status kMine = -42;
    uint8_t buf[8];

    Script read{{kMine, 0}};
    size_t n = kUnset;
    EXPECT_EQ(el_read(read.reader(), buf, sizeof(buf), &n), kMine);
    EXPECT_EQ(n, 0u);

    Script write{{kMine, 0}};
    n = kUnset;
    EXPECT_EQ(el_write(write.writer(), "abcd", 4, &n), kMine);
    EXPECT_EQ(n, 0u);

    Script read_exact{{EL_OK, 3}, {kMine, 0}};
    n = kUnset;
    EXPECT_EQ(el_read_exact(read_exact.reader(), buf, sizeof(buf), &n), kMine);
    EXPECT_EQ(n, 3u);

    Script write_all{{EL_OK, 3}, {kMine, 0}};
    n = kUnset;
    EXPECT_EQ(el_write_all(write_all.writer(), "abcdefgh", 8, &n), kMine);
    EXPECT_EQ(n, 3u);
}

} // namespace
