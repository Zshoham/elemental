/*
 * The io interface contract as tests. Each test names one promise made in
 * elemental/io.h. This file uses nothing but the io module and GoogleTest.
 */
#include <elemental/io_contract.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

namespace
{

constexpr size_t kContentMax = EL_IO_CONTRACT_SIZE_MAX;

/* Consecutive EL_ERR_WOULD_BLOCK results tolerated before giving up. */
constexpr unsigned kStallLimit = 64;

/* A status no implementation returns: the stream broke the contract. */
constexpr el_status kStatusBroken = -1;

/* An output parameter that is not overwritten keeps this value. */
constexpr size_t kCountUnset = static_cast<size_t>(-1);

constexpr size_t kStreamLengths[] = {0, 1, 2, kContentMax - 1, kContentMax};
/* The last chunk size is larger than any stream. */
constexpr size_t kChunkSizes[] = {1, 3, 64, kContentMax + 44};

/* Bytes as a value GoogleTest can compare and print. */
std::vector<uint8_t> Bytes(const uint8_t *data, size_t len)
{
    return std::vector<uint8_t>(data, data + len);
}

/*
 * One test's view of the implementation under test: the fixture, and the
 * buffers the streams carry.
 */
class Harness
{
  public:
    explicit Harness(const el_io_contract_fixture *fixture) : fx_(fixture)
    {
        /* 31 is odd, so i -> 31 * i + 7 (mod 256) visits every byte value
         * once: a dropped, repeated, or reordered byte is visible. */
        for (size_t i = 0; i < kContentMax; i++) {
            content[i] = static_cast<uint8_t>(i * 31u + 7u);
        }
    }

    /* The bytes streams are asked to carry. */
    uint8_t content[kContentMax];
    /* What came out the other end. Larger than any stream, so a stream that
     * delivers too much is caught instead of overrunning the buffer. */
    uint8_t received[kContentMax + 64] = {};
    /* A source buffer the tests are free to overwrite. */
    uint8_t source[kContentMax] = {};

    el_reader OpenReader(size_t len)
    {
        fx_->reset(fx_->ctx);
        return fx_->reader_with_content(fx_->ctx, content, len);
    }

    el_writer OpenWriter(size_t room)
    {
        fx_->reset(fx_->ctx);
        return fx_->writer_with_room(fx_->ctx, room);
    }

    el_reader OpenStalledReader()
    {
        fx_->reset(fx_->ctx);
        return fx_->reader_stalled(fx_->ctx);
    }

    el_writer OpenStalledWriter()
    {
        fx_->reset(fx_->ctx);
        return fx_->writer_stalled(fx_->ctx);
    }

    el_writer OpenWriterEndingAfter(size_t room)
    {
        fx_->reset(fx_->ctx);
        return fx_->writer_ending_after(fx_->ctx, room);
    }

    el_writer OpenWriterFullAfter(size_t room)
    {
        fx_->reset(fx_->ctx);
        return fx_->writer_full_after(fx_->ctx, room);
    }

    /* Moves what the writer's sink received into `received`. */
    size_t TakeWritten()
    {
        return fx_->writer_take(fx_->ctx, received, sizeof(received));
    }

    /* ---- Checked single attempts ----------------------------------------
     *
     * Every read and write the suite performs goes through these, so the
     * "progress or status, never both" rule is checked on every call.
     */

    el_status ReadOnce(el_reader reader, uint8_t *buf, size_t cap, size_t *n)
    {
        *n = kCountUnset;
        const el_status status = el_read(reader, buf, cap, n);
        return Checked("el_read", status, cap, n);
    }

    el_status WriteOnce(el_writer writer, const uint8_t *data, size_t len, size_t *n)
    {
        *n = kCountUnset;
        const el_status status = el_write(writer, data, len, n);
        return Checked("el_write", status, len, n);
    }

    /*
     * Read into `received`, `chunk` bytes at a time, until the stream stops
     * giving. Returns the byte count; *last is the status that stopped it.
     * Reads at most `expect` + 1 bytes, so a reader that never ends
     * terminates the loop with *last == EL_OK.
     */
    size_t ReadUntilStop(el_reader reader, size_t chunk, size_t expect, el_status *last)
    {
        size_t total = 0;
        unsigned stalls = 0;

        *last = EL_OK;
        while (total <= expect) {
            const size_t cap = std::min(chunk, sizeof(received) - total);
            size_t n = 0;
            const el_status status = ReadOnce(reader, received + total, cap, &n);
            if (status == EL_OK) {
                total += n;
                stalls = 0;
            } else if (status == EL_ERR_WOULD_BLOCK && stalls < kStallLimit) {
                stalls++;
            } else {
                *last = status;
                break;
            }
        }
        return total;
    }

    /*
     * Write data[0..len), `chunk` bytes at a time, until done or the stream
     * stops taking. Returns the byte count; *last is EL_OK if all was
     * written.
     */
    size_t WriteUntilStop(el_writer writer, const uint8_t *data, size_t len, size_t chunk,
                          el_status *last)
    {
        size_t total = 0;
        unsigned stalls = 0;

        *last = EL_OK;
        while (total < len) {
            size_t n = 0;
            const el_status status =
                WriteOnce(writer, data + total, std::min(chunk, len - total), &n);
            if (status == EL_OK) {
                total += n;
                stalls = 0;
            } else if (status == EL_ERR_WOULD_BLOCK && stalls < kStallLimit) {
                stalls++;
            } else {
                *last = status;
                break;
            }
        }
        return total;
    }

    /* el_read_exact(), resumed across EL_ERR_WOULD_BLOCK as documented. */
    size_t ReadExactResuming(el_reader reader, uint8_t *buf, size_t len, el_status *last)
    {
        size_t total = 0;
        unsigned stalls = 0;

        for (;;) {
            size_t n = kCountUnset;
            const el_status status = el_read_exact(reader, buf + total, len - total, &n);
            if (!CheckedUntilDone("el_read_exact", status, len - total, n)) {
                *last = kStatusBroken;
                return total;
            }
            total += n;
            stalls = n > 0 ? 0 : stalls + 1;
            if (status != EL_ERR_WOULD_BLOCK || stalls > kStallLimit) {
                *last = status;
                return total;
            }
        }
    }

    /* el_write_all(), resumed across EL_ERR_WOULD_BLOCK as documented. */
    size_t WriteAllResuming(el_writer writer, const uint8_t *data, size_t len, el_status *last)
    {
        size_t total = 0;
        unsigned stalls = 0;

        for (;;) {
            size_t n = kCountUnset;
            const el_status status = el_write_all(writer, data + total, len - total, &n);
            if (!CheckedUntilDone("el_write_all", status, len - total, n)) {
                *last = kStatusBroken;
                return total;
            }
            total += n;
            stalls = n > 0 ? 0 : stalls + 1;
            if (status != EL_ERR_WOULD_BLOCK || stalls > kStallLimit) {
                *last = status;
                return total;
            }
        }
    }

  private:
    /* A single attempt: the count is written, and progress excludes status. */
    static el_status Checked(const char *call, el_status status, size_t asked, size_t *n)
    {
        const bool written = *n != kCountUnset;
        EXPECT_TRUE(written) << call << "() did not write the count";

        const bool consistent = status == EL_OK ? (*n >= 1 && *n <= asked) : (*n == 0);
        EXPECT_TRUE(consistent) << call << "() returned status " << status << " with a count of "
                                << *n << " for a request of " << asked
                                << ": progress or status, never both";

        if (!written || !consistent) {
            *n = 0;
            return kStatusBroken;
        }
        return status;
    }

    /* An until-done helper: the count is in range, and EL_OK means all of it. */
    static bool CheckedUntilDone(const char *call, el_status status, size_t asked, size_t n)
    {
        const bool in_range = n != kCountUnset && n <= asked;
        EXPECT_TRUE(in_range) << call << "() reported a count of " << n << " for a request of "
                              << asked;
        if (!in_range) {
            return false;
        }
        EXPECT_EQ(status == EL_OK, n == asked)
            << call << "() returned status " << status << " with a count of " << n
            << " for a request of " << asked;
        return true;
    }

    const el_io_contract_fixture *fx_;
};

} // namespace

void ElIoContract::SetUp()
{
    const el_io_contract_fixture *const fixture = GetParam();
    ASSERT_TRUE(fixture != nullptr) << "the suite was instantiated with a null fixture";
    ASSERT_TRUE(fixture->reset != nullptr) << "fixture.reset is required";
    ASSERT_TRUE(fixture->reader_with_content != nullptr)
        << "fixture.reader_with_content is required";
    ASSERT_TRUE(fixture->writer_with_room != nullptr) << "fixture.writer_with_room is required";
    ASSERT_TRUE(fixture->writer_take != nullptr) << "fixture.writer_take is required";
}

void ElIoContract::TearDown()
{
    const el_io_contract_fixture *const fixture = GetParam();
    if (fixture != nullptr && fixture->reset != nullptr) {
        fixture->reset(fixture->ctx);
    }
}

void PrintTo(const el_io_contract_fixture *fixture, std::ostream *os)
{
    if (fixture == nullptr) {
        *os << "(null fixture)";
    } else if (fixture->name == nullptr) {
        *os << "(unnamed fixture)";
    } else {
        *os << fixture->name;
    }
}

/* ---- Reader tests ------------------------------------------------------- */

TEST_P(ElIoContract, ReaderDeliversItsContentInOrderThenEnds)
{
    Harness h(GetParam());

    for (const size_t len : kStreamLengths) {
        for (const size_t chunk : kChunkSizes) {
            SCOPED_TRACE(testing::Message()
                         << "a stream of " << len << " bytes, read " << chunk << " at a time");
            const el_reader reader = h.OpenReader(len);

            el_status last;
            const size_t total = h.ReadUntilStop(reader, chunk, len, &last);

            EXPECT_EQ(last, EL_ERR_END_OF_STREAM);
            EXPECT_EQ(total, len);
            const size_t common = std::min(total, len);
            EXPECT_EQ(Bytes(h.received, common), Bytes(h.content, common));
        }
    }
}

TEST_P(ElIoContract, ReaderEndOfStreamIsPermanent)
{
    Harness h(GetParam());
    const el_reader reader = h.OpenReader(4);

    el_status last;
    (void)h.ReadUntilStop(reader, 4, 4, &last);
    EXPECT_EQ(last, EL_ERR_END_OF_STREAM);

    for (int i = 0; i < 3; i++) {
        uint8_t byte;
        size_t n;
        EXPECT_EQ(h.ReadOnce(reader, &byte, 1, &n), EL_ERR_END_OF_STREAM);
    }
}

TEST_P(ElIoContract, ReaderZeroCapacityReadIsANoOp)
{
    Harness h(GetParam());
    const el_reader reader = h.OpenReader(8);

    /* Mid-stream: succeeds, moves nothing, consumes nothing. */
    size_t n = kCountUnset;
    EXPECT_EQ(el_read(reader, nullptr, 0, &n), EL_OK);
    EXPECT_EQ(n, 0u);

    el_status last;
    const size_t total = h.ReadUntilStop(reader, 8, 8, &last);
    EXPECT_EQ(last, EL_ERR_END_OF_STREAM);
    EXPECT_EQ(total, 8u);
    const size_t common = std::min<size_t>(total, 8);
    EXPECT_EQ(Bytes(h.received, common), Bytes(h.content, common));

    /* At the end: still succeeds, so it cannot be used to detect the end. */
    n = kCountUnset;
    EXPECT_EQ(el_read(reader, nullptr, 0, &n), EL_OK);
    EXPECT_EQ(n, 0u);
}

TEST_P(ElIoContract, ReadExactFillsTheBufferOrReportsAShortStream)
{
    Harness h(GetParam());
    const el_reader reader = h.OpenReader(kContentMax);

    el_status last;
    size_t total = h.ReadExactResuming(reader, h.received, 100, &last);
    EXPECT_EQ(last, EL_OK);
    EXPECT_EQ(total, 100u);
    size_t common = std::min<size_t>(total, 100);
    EXPECT_EQ(Bytes(h.received, common), Bytes(h.content, common));

    /* Asking for more than remains: the status, and how much was read. */
    const size_t remaining = kContentMax - 100;
    total = h.ReadExactResuming(reader, h.received, 200, &last);
    EXPECT_EQ(last, EL_ERR_END_OF_STREAM);
    EXPECT_EQ(total, remaining);
    common = std::min(total, remaining);
    EXPECT_EQ(Bytes(h.received, common), Bytes(h.content + 100, common));

    /* Asking for nothing succeeds without consulting the stream. */
    size_t n = kCountUnset;
    EXPECT_EQ(el_read_exact(reader, nullptr, 0, &n), EL_OK);
    EXPECT_EQ(n, 0u);
}

TEST_P(ElIoContract, StalledReaderReportsWouldBlock)
{
    if (GetParam()->reader_stalled == nullptr) {
        GTEST_SKIP() << "the fixture has no reader_stalled: this reader cannot stall";
    }
    Harness h(GetParam());
    const el_reader reader = h.OpenStalledReader();

    for (int i = 0; i < 3; i++) {
        size_t n;
        EXPECT_EQ(h.ReadOnce(reader, h.received, 8, &n), EL_ERR_WOULD_BLOCK);
    }

    size_t n = kCountUnset;
    EXPECT_EQ(el_read_exact(reader, h.received, 8, &n), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(n, 0u);
}

/* ---- Writer tests ------------------------------------------------------- */

TEST_P(ElIoContract, WriterAcceptsContentInOrder)
{
    Harness h(GetParam());

    for (const size_t len : kStreamLengths) {
        if (len == 0) {
            continue; /* Covered by the zero-length test. */
        }
        for (const size_t chunk : kChunkSizes) {
            SCOPED_TRACE(testing::Message() << len << " bytes, written " << chunk << " at a time");
            const el_writer writer = h.OpenWriter(len);

            el_status last;
            const size_t total = h.WriteUntilStop(writer, h.content, len, chunk, &last);
            EXPECT_EQ(last, EL_OK);
            EXPECT_EQ(total, len);

            const size_t taken = h.TakeWritten();
            EXPECT_EQ(taken, len);
            const size_t common = std::min(taken, len);
            EXPECT_EQ(Bytes(h.received, common), Bytes(h.content, common));
        }
    }
}

TEST_P(ElIoContract, WriterZeroLengthWriteIsANoOp)
{
    Harness h(GetParam());
    const el_writer writer = h.OpenWriter(8);

    size_t n = kCountUnset;
    EXPECT_EQ(el_write(writer, nullptr, 0, &n), EL_OK);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(h.TakeWritten(), 0u);

    n = kCountUnset;
    EXPECT_EQ(el_write_all(writer, nullptr, 0, &n), EL_OK);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(h.TakeWritten(), 0u);
}

TEST_P(ElIoContract, WriterCopiesWhatItAccepts)
{
    Harness h(GetParam());
    const size_t len = 32;
    const el_writer writer = h.OpenWriter(len);

    std::copy(h.content, h.content + len, h.source);

    el_status last;
    const size_t total = h.WriteAllResuming(writer, h.source, len, &last);
    EXPECT_EQ(last, EL_OK);
    EXPECT_EQ(total, len);

    /* The caller's buffer is its own again as soon as the call returns. */
    std::fill(h.source, h.source + len, uint8_t{0xFF});

    const size_t taken = h.TakeWritten();
    EXPECT_EQ(taken, len);
    const size_t common = std::min(taken, len);
    EXPECT_EQ(Bytes(h.received, common), Bytes(h.content, common));
}

TEST_P(ElIoContract, WriteAllWritesEverything)
{
    Harness h(GetParam());
    const el_writer writer = h.OpenWriter(kContentMax);

    el_status last;
    const size_t total = h.WriteAllResuming(writer, h.content, kContentMax, &last);
    EXPECT_EQ(last, EL_OK);
    EXPECT_EQ(total, kContentMax);

    const size_t taken = h.TakeWritten();
    EXPECT_EQ(taken, kContentMax);
    const size_t common = std::min(taken, kContentMax);
    EXPECT_EQ(Bytes(h.received, common), Bytes(h.content, common));
}

TEST_P(ElIoContract, WriterEndOfStreamIsPermanentAndKeepsWhatFit)
{
    if (GetParam()->writer_ending_after == nullptr) {
        GTEST_SKIP() << "the fixture has no writer_ending_after: this writer cannot be made to "
                        "end after an exact byte count";
    }
    Harness h(GetParam());
    const size_t room = 10;
    const el_writer writer = h.OpenWriterEndingAfter(room);

    /* More than fits: the part that fit is reported alongside the status. */
    el_status last;
    const size_t total = h.WriteAllResuming(writer, h.content, 16, &last);
    EXPECT_EQ(last, EL_ERR_END_OF_STREAM);
    EXPECT_EQ(total, room);

    for (int i = 0; i < 3; i++) {
        size_t n;
        EXPECT_EQ(h.WriteOnce(writer, h.content, 1, &n), EL_ERR_END_OF_STREAM);
    }

    const size_t taken = h.TakeWritten();
    EXPECT_EQ(taken, room);
    const size_t common = std::min(taken, room);
    EXPECT_EQ(Bytes(h.received, common), Bytes(h.content, common));

    /* No room at all: ended from the first write. */
    const el_writer full = h.OpenWriterEndingAfter(0);
    size_t n;
    EXPECT_EQ(h.WriteOnce(full, h.content, 1, &n), EL_ERR_END_OF_STREAM);
    EXPECT_EQ(h.TakeWritten(), 0u);
}

TEST_P(ElIoContract, WriterNoSpaceKeepsWhatFitAndRepeats)
{
    if (GetParam()->writer_full_after == nullptr) {
        GTEST_SKIP() << "the fixture has no writer_full_after: this writer cannot be made full "
                        "after an exact byte count";
    }
    Harness h(GetParam());
    const size_t room = 10;
    const el_writer writer = h.OpenWriterFullAfter(room);

    /* More than fits: the part that fit is reported alongside the status. */
    el_status last;
    const size_t total = h.WriteAllResuming(writer, h.content, 16, &last);
    EXPECT_EQ(last, EL_ERR_NO_SPACE);
    EXPECT_EQ(total, room);

    /* Full stays full while nothing makes room, and takes nothing. */
    for (int i = 0; i < 3; i++) {
        size_t n;
        EXPECT_EQ(h.WriteOnce(writer, h.content + room, 1, &n), EL_ERR_NO_SPACE);
    }

    const size_t taken = h.TakeWritten();
    EXPECT_EQ(taken, room);
    const size_t common = std::min(taken, room);
    EXPECT_EQ(Bytes(h.received, common), Bytes(h.content, common));

    /* No room at all: full from the first write. */
    const el_writer full = h.OpenWriterFullAfter(0);
    size_t n;
    EXPECT_EQ(h.WriteOnce(full, h.content, 1, &n), EL_ERR_NO_SPACE);
    EXPECT_EQ(h.TakeWritten(), 0u);
}

TEST_P(ElIoContract, StalledWriterReportsWouldBlock)
{
    if (GetParam()->writer_stalled == nullptr) {
        GTEST_SKIP() << "the fixture has no writer_stalled: this writer cannot stall";
    }
    Harness h(GetParam());
    const el_writer writer = h.OpenStalledWriter();

    for (int i = 0; i < 3; i++) {
        size_t n;
        EXPECT_EQ(h.WriteOnce(writer, h.content, 8, &n), EL_ERR_WOULD_BLOCK);
    }

    size_t n = kCountUnset;
    EXPECT_EQ(el_write_all(writer, h.content, 8, &n), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(n, 0u);
}
