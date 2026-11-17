/*
 * What el_ring promises with one context driving both sides
 * (elemental/ring.h): every byte of the storage holds data, bytes come out
 * in the order they went in across the end of the storage, what room and
 * len report, closing, and the statuses of its reader and writer. What two
 * contexts do is ring_stress_test.cpp.
 */
#include <gtest/gtest.h>

#include <elemental/ring.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace
{

constexpr size_t kUnset = static_cast<size_t>(-1);

std::string ReadAll(el_ring *ring)
{
    char bytes[64];
    const size_t n = el_ring_read(ring, bytes, sizeof(bytes));
    return std::string(bytes, n);
}

TEST(Ring, StartsEmptyAndOpenWithAllItsRoom)
{
    uint8_t storage[8];
    el_ring ring;
    el_ring_init(&ring, storage, sizeof(storage));

    EXPECT_EQ(el_ring_len(&ring), 0u);
    EXPECT_EQ(el_ring_room(&ring), 8u);
    uint8_t byte = 0x5a;
    EXPECT_EQ(el_ring_read(&ring, &byte, 1), 0u);
    EXPECT_EQ(byte, 0x5a) << "an empty ring wrote to the caller's buffer";
}

TEST(Ring, EveryByteOfTheStorageHoldsData)
{
    uint8_t storage[4];
    el_ring ring;
    el_ring_init(&ring, storage, sizeof(storage));

    EXPECT_EQ(el_ring_write(&ring, "abcd", 4), 4u);
    EXPECT_EQ(el_ring_len(&ring), 4u);
    EXPECT_EQ(el_ring_room(&ring), 0u);
    EXPECT_EQ(ReadAll(&ring), "abcd");
}

TEST(Ring, AWriteTakesWhatFitsAndSaysHowMuch)
{
    uint8_t storage[4];
    el_ring ring;
    el_ring_init(&ring, storage, sizeof(storage));

    EXPECT_EQ(el_ring_write(&ring, "abcdef", 6), 4u);
    EXPECT_EQ(el_ring_write(&ring, "ef", 2), 0u) << "a full ring took bytes";
    EXPECT_EQ(ReadAll(&ring), "abcd");
}

TEST(Ring, AReadGivesWhatIsHeldOldestFirstAndSaysHowMuch)
{
    uint8_t storage[8];
    el_ring ring;
    el_ring_init(&ring, storage, sizeof(storage));
    ASSERT_EQ(el_ring_write(&ring, "abcde", 5), 5u);

    char bytes[8] = {};
    EXPECT_EQ(el_ring_read(&ring, bytes, 2), 2u);
    EXPECT_EQ(std::string(bytes, 2), "ab");
    EXPECT_EQ(el_ring_len(&ring), 3u);
    EXPECT_EQ(el_ring_room(&ring), 5u);

    EXPECT_EQ(el_ring_read(&ring, bytes, sizeof(bytes)), 3u);
    EXPECT_EQ(std::string(bytes, 3), "cde");
    EXPECT_EQ(el_ring_read(&ring, bytes, sizeof(bytes)), 0u);
}

TEST(Ring, RoomAndLenFollowEachWriteAndRead)
{
    uint8_t storage[5];
    el_ring ring;
    el_ring_init(&ring, storage, sizeof(storage));

    size_t held = 0;
    /* Uneven steps, so that the ring passes through full and empty at
     * different places in the storage. */
    static const size_t steps[] = {3, 2, 5, 1, 4, 5, 2, 3, 1, 5};
    for (int round = 0; round < 30; round++) {
        SCOPED_TRACE(testing::Message() << "round " << round);
        const size_t step = steps[round % 10];
        uint8_t bytes[5] = {};

        const size_t wrote = el_ring_write(&ring, bytes, step);
        ASSERT_EQ(wrote, step < 5 - held ? step : 5 - held);
        held += wrote;
        ASSERT_EQ(el_ring_len(&ring), held);
        ASSERT_EQ(el_ring_room(&ring), 5 - held);

        const size_t read = el_ring_read(&ring, bytes, steps[(round + 3) % 10]);
        ASSERT_EQ(read, steps[(round + 3) % 10] < held ? steps[(round + 3) % 10] : held);
        held -= read;
        ASSERT_EQ(el_ring_len(&ring), held);
        ASSERT_EQ(el_ring_room(&ring), 5 - held);
    }
}

/* For each capacity: from every value the two indices can have, write and
 * read every size, so that every way a copy can be split by the end of the
 * storage is taken. */
TEST(Ring, BytesArriveInOrderFromEveryPositionAndInEverySize)
{
    for (const size_t cap : {size_t{1}, size_t{2}, size_t{3}, size_t{7}, size_t{16}}) {
        for (size_t start = 0; start < 2 * cap; start++) {
            for (size_t size = 0; size <= cap; size++) {
                SCOPED_TRACE(testing::Message()
                             << "capacity " << cap << ", start " << start << ", size " << size);
                /* The storage sits between guard bytes. */
                uint8_t guarded[1 + 16 + 1];
                memset(guarded, 0xee, sizeof(guarded));
                el_ring ring;
                el_ring_init(&ring, guarded + 1, cap);

                /* The indices count modulo twice the capacity. */
                for (size_t i = 0; i < start; i++) {
                    uint8_t one = 0;
                    ASSERT_EQ(el_ring_write(&ring, &one, 1), 1u);
                    ASSERT_EQ(el_ring_read(&ring, &one, 1), 1u);
                }

                uint8_t chunk[16];
                for (size_t i = 0; i < size; i++) {
                    chunk[i] = static_cast<uint8_t>(0x30 + i);
                }
                ASSERT_EQ(el_ring_write(&ring, chunk, size), size);
                ASSERT_EQ(el_ring_len(&ring), size);
                ASSERT_EQ(el_ring_room(&ring), cap - size);

                uint8_t back[16];
                ASSERT_EQ(el_ring_read(&ring, back, sizeof(back)), size);
                for (size_t i = 0; i < size; i++) {
                    ASSERT_EQ(back[i], 0x30 + i) << "byte " << i;
                }
                ASSERT_EQ(el_ring_len(&ring), 0u);
                ASSERT_EQ(el_ring_room(&ring), cap);

                ASSERT_EQ(guarded[0], 0xee) << "wrote before the storage";
                for (size_t i = 1 + cap; i < sizeof(guarded); i++) {
                    ASSERT_EQ(guarded[i], 0xee) << "wrote after the storage";
                }
            }
        }
    }
}

/* A ring that is never empty: what is read was written a lap of the
 * storage earlier. */
TEST(Ring, BytesHeldAcrossTheEndOfTheStorageKeepTheirOrder)
{
    uint8_t storage[7];
    el_ring ring;
    el_ring_init(&ring, storage, sizeof(storage));

    uint8_t next_in = 0;
    uint8_t next_out = 0;
    uint8_t chunk[5];
    for (int round = 0; round < 100; round++) {
        SCOPED_TRACE(testing::Message() << "round " << round);
        /* As much as fits of five bytes in, three out. */
        for (uint8_t &byte : chunk) {
            byte = next_in++;
        }
        const size_t wrote = el_ring_write(&ring, chunk, sizeof(chunk));
        next_in = static_cast<uint8_t>(next_in - (sizeof(chunk) - wrote));

        uint8_t back[3];
        const size_t read = el_ring_read(&ring, back, sizeof(back));
        ASSERT_EQ(read, 3u);
        for (const uint8_t byte : back) {
            ASSERT_EQ(byte, next_out++);
        }
    }
    EXPECT_EQ(el_ring_len(&ring), 4u) << "the ring was meant to be full before each read";
}

TEST(Ring, ARingWithoutStorageIsAlwaysFullAndEmpty)
{
    el_ring ring;
    el_ring_init(&ring, nullptr, 0);

    EXPECT_EQ(el_ring_room(&ring), 0u);
    EXPECT_EQ(el_ring_len(&ring), 0u);
    EXPECT_EQ(el_ring_write(&ring, "a", 1), 0u);
    char byte;
    EXPECT_EQ(el_ring_read(&ring, &byte, 1), 0u);

    size_t n = kUnset;
    EXPECT_EQ(el_write(el_ring_as_writer(&ring), "a", 1, &n), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(n, 0u);
    n = kUnset;
    EXPECT_EQ(el_read(el_ring_as_reader(&ring), &byte, 1, &n), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(n, 0u);
}

TEST(Ring, NothingMovesForASizeOfZeroAndTheBufferMayBeNull)
{
    uint8_t storage[4];
    el_ring ring;
    el_ring_init(&ring, storage, sizeof(storage));
    ASSERT_EQ(el_ring_write(&ring, "ab", 2), 2u);

    EXPECT_EQ(el_ring_write(&ring, nullptr, 0), 0u);
    EXPECT_EQ(el_ring_read(&ring, nullptr, 0), 0u);
    EXPECT_EQ(el_ring_len(&ring), 2u);
    EXPECT_EQ(ReadAll(&ring), "ab");
}

TEST(Ring, ClosingStopsWritesAndKeepsWhatWasWritten)
{
    uint8_t storage[8];
    el_ring ring;
    el_ring_init(&ring, storage, sizeof(storage));
    ASSERT_EQ(el_ring_write(&ring, "abc", 3), 3u);

    el_ring_close(&ring);
    EXPECT_EQ(el_ring_write(&ring, "d", 1), 0u) << "a closed ring took a byte";
    EXPECT_EQ(el_ring_room(&ring), 0u) << "room is what a write would take";
    EXPECT_EQ(el_ring_len(&ring), 3u);

    el_ring_close(&ring); /* Twice is once. */
    EXPECT_EQ(ReadAll(&ring), "abc");
    EXPECT_EQ(el_ring_len(&ring), 0u);
}

TEST(Ring, SettingARingUpAgainEmptiesAndReopensIt)
{
    uint8_t storage[4];
    el_ring ring;
    el_ring_init(&ring, storage, sizeof(storage));
    ASSERT_EQ(el_ring_write(&ring, "abc", 3), 3u);
    el_ring_close(&ring);

    el_ring_init(&ring, storage, sizeof(storage));
    EXPECT_EQ(el_ring_len(&ring), 0u);
    EXPECT_EQ(el_ring_room(&ring), 4u);
    EXPECT_EQ(el_ring_write(&ring, "wxyz", 4), 4u);
    EXPECT_EQ(ReadAll(&ring), "wxyz");
}

/* ---- The two sides as streams ------------------------------------------- */

TEST(Ring, ClosingEndsTheWriterAtOnceAndTheReaderAfterTheLastByte)
{
    uint8_t storage[8];
    el_ring ring;
    el_ring_init(&ring, storage, sizeof(storage));
    const el_reader reader = el_ring_as_reader(&ring);
    const el_writer writer = el_ring_as_writer(&ring);

    size_t n = kUnset;
    EXPECT_EQ(el_write_all(writer, "abc", 3, &n), EL_OK);
    el_ring_close(&ring);

    /* The writer has ended, even though there is room. */
    n = kUnset;
    EXPECT_EQ(el_write(writer, "d", 1, &n), EL_ERR_END_OF_STREAM);
    EXPECT_EQ(n, 0u);

    /* The reader still gets what was written before the close, then ends,
     * and stays ended. */
    char buf[8];
    n = kUnset;
    EXPECT_EQ(el_read(reader, buf, 2, &n), EL_OK);
    EXPECT_EQ(n, 2u);
    n = kUnset;
    EXPECT_EQ(el_read_exact(reader, buf + 2, sizeof(buf) - 2, &n), EL_ERR_END_OF_STREAM);
    ASSERT_EQ(n, 1u);
    EXPECT_EQ(std::string(buf, 3), "abc");
    n = kUnset;
    EXPECT_EQ(el_read(reader, buf, sizeof(buf), &n), EL_ERR_END_OF_STREAM);
    EXPECT_EQ(n, 0u);
}

TEST(Ring, AnEmptyRingStallsItsReaderUntilItIsClosed)
{
    uint8_t storage[8];
    el_ring ring;
    el_ring_init(&ring, storage, sizeof(storage));
    const el_reader reader = el_ring_as_reader(&ring);

    char byte;
    size_t n = kUnset;
    EXPECT_EQ(el_read(reader, &byte, 1, &n), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(n, 0u);

    /* The same call succeeds once the producer has written. */
    ASSERT_EQ(el_ring_write(&ring, "a", 1), 1u);
    n = kUnset;
    EXPECT_EQ(el_read(reader, &byte, 1, &n), EL_OK);
    EXPECT_EQ(n, 1u);
    EXPECT_EQ(byte, 'a');

    n = kUnset;
    EXPECT_EQ(el_read(reader, &byte, 1, &n), EL_ERR_WOULD_BLOCK);
    el_ring_close(&ring);
    n = kUnset;
    EXPECT_EQ(el_read(reader, &byte, 1, &n), EL_ERR_END_OF_STREAM);
    EXPECT_EQ(n, 0u);
}

/* Full is a stall, EL_ERR_WOULD_BLOCK, and not EL_ERR_NO_SPACE: the room
 * comes back when the other side reads (docs/proposals/ring.md, decision 2). */
TEST(Ring, AFullRingStallsItsWriterAndTakesTheRestOnceRead)
{
    uint8_t storage[4];
    el_ring ring;
    el_ring_init(&ring, storage, sizeof(storage));
    const el_reader reader = el_ring_as_reader(&ring);
    const el_writer writer = el_ring_as_writer(&ring);

    /* Six bytes into four: write_all reports the stall and what fit. */
    static const char message[] = "abcdef";
    size_t n = kUnset;
    EXPECT_EQ(el_write_all(writer, message, 6, &n), EL_ERR_WOULD_BLOCK);
    ASSERT_EQ(n, 4u);

    char buf[4];
    size_t got = kUnset;
    EXPECT_EQ(el_read(reader, buf, 2, &got), EL_OK);
    EXPECT_EQ(got, 2u);

    /* Resume with the remainder, exactly as elemental/io.h describes. */
    size_t more = kUnset;
    EXPECT_EQ(el_write_all(writer, message + n, 6 - n, &more), EL_OK);
    EXPECT_EQ(more, 2u);

    got = kUnset;
    EXPECT_EQ(el_read_exact(reader, buf, 4, &got), EL_OK);
    EXPECT_EQ(std::string(buf, 4), "cdef");
}

} // namespace
