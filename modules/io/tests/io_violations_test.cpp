/*
 * Contract violations are caught, on both sides of the interface: callers
 * that break a precondition, and implementations that break the operation
 * contract.
 *
 * These are death tests: each violation runs in a child process, and the
 * test passes if that process is stopped. So they hold in every assertion
 * configuration, whether a failed check traps in place or calls the
 * application's handler (which, in tests, reports and aborts).
 */
#include <gtest/gtest.h>

#include <elemental/io.h>
#include <elemental/io_buf.h>
#include <elemental/io_mem.h>

#include <cstddef>
#include <cstdint>

#if __has_include(<sys/resource.h>)
#include <sys/resource.h>
#define HAVE_RLIMIT 1
#endif
#if __has_include(<sys/prctl.h>)
#include <sys/prctl.h>
#define HAVE_PRCTL 1
#endif

namespace
{

/* How a violation ends depends on the configuration, so any death counts. */
#define EXPECT_VIOLATION(statement) EXPECT_DEATH(statement, "")

/* ---- Implementations that break the operation contract ------------------ */

el_status ReadOkWithoutProgress(void *, void *, size_t, size_t *)
{
    return EL_OK;
}

el_status ReadMoreThanAsked(void *, void *, size_t cap, size_t *n)
{
    *n = cap + 1;
    return EL_OK;
}

el_status ReadFailureWithProgress(void *, void *, size_t, size_t *n)
{
    *n = 1;
    return EL_ERR_IO;
}

el_status WriteOkWithoutProgress(void *, const void *, size_t, size_t *)
{
    return EL_OK;
}

el_status WriteMoreThanAsked(void *, const void *, size_t len, size_t *n)
{
    *n = len + 1;
    return EL_OK;
}

el_status WriteFailureWithProgress(void *, const void *, size_t, size_t *n)
{
    *n = 1;
    return EL_ERR_WOULD_BLOCK;
}

el_reader BrokenReader(el_status (*read)(void *, void *, size_t, size_t *))
{
    /* One table per call site is enough: each death test uses one. */
    static el_reader_ops ops;
    ops.read = read;
    return el_reader{nullptr, &ops};
}

el_writer BrokenWriter(el_status (*write)(void *, const void *, size_t, size_t *))
{
    static el_writer_ops ops;
    ops.write = write;
    return el_writer{nullptr, &ops};
}

class IoViolations : public testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        /* A failed check kills the child with a signal. That is the expected
         * outcome, so it should leave neither a core file nor a crash record
         * behind. The limit covers core files; on Linux, a crash handler that
         * core dumps are piped to ignores the limit, so the process is also
         * marked as not dumpable. */
#ifdef HAVE_RLIMIT
        const rlimit no_core = {0, 0};
        setrlimit(RLIMIT_CORE, &no_core);
#endif
#ifdef HAVE_PRCTL
        prctl(PR_SET_DUMPABLE, 0);
#endif
    }

    void SetUp() override
    {
        el_mem_reader_init(&mem_reader, content, sizeof(content));
        el_buf_init(&mem, sink, sizeof(sink));
        reader = el_mem_reader_as_reader(&mem_reader);
        writer = el_buf_as_writer(&mem);
    }

    const uint8_t content[4] = {1, 2, 3, 4};
    uint8_t sink[4] = {};
    uint8_t buf[8] = {};
    size_t n = 0;
    el_mem_reader mem_reader;
    el_buf mem;
    el_reader reader;
    el_writer writer;
};

/* GoogleTest runs suites named *DeathTest first, before any threads exist. */
using IoViolationsDeathTest = IoViolations;

TEST_F(IoViolations, CorrectUseDoesNotTripACheck)
{
    /* Including the edge cases. A check that fired would end the test. */
    EXPECT_EQ(el_read(reader, buf, 1, &n), EL_OK);
    EXPECT_EQ(el_read(reader, nullptr, 0, &n), EL_OK);
    EXPECT_EQ(el_write(writer, content, 1, &n), EL_OK);
    EXPECT_EQ(el_write(writer, nullptr, 0, &n), EL_OK);
    /* The count is optional on the helpers that loop, and only there. */
    EXPECT_EQ(el_read_exact(reader, buf, 1, nullptr), EL_OK);
    EXPECT_EQ(el_write_all(writer, content, 1, nullptr), EL_OK);
    el_mem_reader_init(&mem_reader, nullptr, 0);
    el_buf_init(&mem, nullptr, 0);

    /* The buffer, at its edges. */
    el_buf_init(&mem, sink, sizeof(sink));
    EXPECT_EQ(el_buf_append(&mem, nullptr, 0), EL_OK);
    EXPECT_EQ(el_buf_append(&mem, content, 4), EL_OK);
    el_buf_commit(&mem, 0);
    el_buf_truncate(&mem, 4);
    el_buf_truncate(&mem, 0);
    el_buf_commit(&mem, 4);
    /* Bytes next to the storage, not in it. */
    uint8_t around[6] = {};
    el_buf_init(&mem, around + 1, 4);
    EXPECT_EQ(el_buf_append(&mem, around + 5, 1), EL_OK);
    EXPECT_EQ(el_buf_append(&mem, around, 1), EL_OK);
}

TEST_F(IoViolationsDeathTest, CallerOmitsTheOutputParameter)
{
    EXPECT_VIOLATION(el_read(reader, buf, sizeof(buf), nullptr));
    EXPECT_VIOLATION(el_write(writer, content, sizeof(content), nullptr));
}

TEST_F(IoViolationsDeathTest, CallerPassesANullBufferWithANonzeroSize)
{
    EXPECT_VIOLATION(el_read(reader, nullptr, 1, &n));
    EXPECT_VIOLATION(el_write(writer, nullptr, 1, &n));
    EXPECT_VIOLATION(el_read_exact(reader, nullptr, 1, &n));
    EXPECT_VIOLATION(el_write_all(writer, nullptr, 1, &n));
}

TEST_F(IoViolationsDeathTest, CallerUsesAHandleThatWasNeverMade)
{
    EXPECT_VIOLATION(el_read(el_reader{nullptr, nullptr}, buf, 1, &n));
    EXPECT_VIOLATION(el_write(el_writer{nullptr, nullptr}, content, 1, &n));
    EXPECT_VIOLATION(el_read(BrokenReader(nullptr), buf, 1, &n));
    EXPECT_VIOLATION(el_write(BrokenWriter(nullptr), content, 1, &n));
}

TEST_F(IoViolationsDeathTest, CallerBreaksAMemoryStreamPrecondition)
{
    EXPECT_VIOLATION(el_mem_reader_init(nullptr, content, sizeof(content)));
    EXPECT_VIOLATION(el_mem_reader_init(&mem_reader, nullptr, 1));
    EXPECT_VIOLATION((void)el_mem_reader_as_reader(nullptr));
    EXPECT_VIOLATION((void)el_mem_reader_remaining(nullptr));
}

TEST_F(IoViolationsDeathTest, CallerBreaksABufferPrecondition)
{
    EXPECT_VIOLATION(el_buf_init(nullptr, sink, sizeof(sink)));
    EXPECT_VIOLATION(el_buf_init(&mem, nullptr, 1));
    EXPECT_VIOLATION((void)el_buf_data(nullptr));
    EXPECT_VIOLATION((void)el_buf_data_mut(nullptr));
    EXPECT_VIOLATION((void)el_buf_len(nullptr));
    EXPECT_VIOLATION((void)el_buf_room(nullptr));
    EXPECT_VIOLATION((void)el_buf_append(nullptr, content, 1));
    EXPECT_VIOLATION((void)el_buf_append(&mem, nullptr, 1));
    EXPECT_VIOLATION((void)el_buf_reserve(nullptr, 1));
    EXPECT_VIOLATION((void)el_buf_spare(nullptr));
    EXPECT_VIOLATION(el_buf_commit(nullptr, 0));
    EXPECT_VIOLATION(el_buf_truncate(nullptr, 0));
    EXPECT_VIOLATION((void)el_buf_as_writer(nullptr));
}

TEST_F(IoViolationsDeathTest, CallerAppendsFromTheBuffersOwnStorage)
{
    ASSERT_EQ(el_buf_append(&mem, content, 2), EL_OK);
    EXPECT_VIOLATION((void)el_buf_append(&mem, el_buf_data(&mem), 1));
    EXPECT_VIOLATION((void)el_buf_append(&mem, sink + 3, 1));
    EXPECT_VIOLATION(el_write(writer, sink, 1, &n));
}

TEST_F(IoViolationsDeathTest, CallerCommitsOrTruncatesPastTheBuffer)
{
    ASSERT_EQ(el_buf_append(&mem, content, 2), EL_OK);
    EXPECT_VIOLATION(el_buf_commit(&mem, 3));
    EXPECT_VIOLATION(el_buf_truncate(&mem, 3));
}

/* "Progress or status, never both", and never more than was asked for.
 * Caught at the boundary, before the caller sees it. */

TEST_F(IoViolationsDeathTest, ReaderBreaksTheOperationContract)
{
    EXPECT_VIOLATION(el_read(BrokenReader(ReadOkWithoutProgress), buf, 4, &n));
    EXPECT_VIOLATION(el_read(BrokenReader(ReadMoreThanAsked), buf, 4, &n));
    EXPECT_VIOLATION(el_read(BrokenReader(ReadFailureWithProgress), buf, 4, &n));
}

TEST_F(IoViolationsDeathTest, WriterBreaksTheOperationContract)
{
    EXPECT_VIOLATION(el_write(BrokenWriter(WriteOkWithoutProgress), content, 4, &n));
    EXPECT_VIOLATION(el_write(BrokenWriter(WriteMoreThanAsked), content, 4, &n));
    EXPECT_VIOLATION(el_write(BrokenWriter(WriteFailureWithProgress), content, 4, &n));
}

} // namespace
