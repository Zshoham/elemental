/*
 * Two contexts at once: one thread writes while another reads
 * (elemental/ring.h, "Two sides"). Host only: it needs threads.
 *
 * The bytes must arrive whole and in order. That check alone passes on
 * x86-64 even against a ring whose indices are not atomic
 * (docs/proposals/ring.md, "What the evidence will and will not show"), so
 * the run that counts is the one under ThreadSanitizer, the `tsan` preset,
 * which reports the race. The test is the same in every preset.
 */
#include <gtest/gtest.h>

#include <elemental/ring.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace
{

/* The byte at `index` of the stream: a pattern with a period that no
 * capacity or chunk size below divides. */
uint8_t ByteAt(size_t index)
{
    return static_cast<uint8_t>(index % 251);
}

/*
 * A broken ring can stop moving bytes altogether. Each side then gives up
 * and tells the other, so that the test fails and does not hang. Asked only
 * when a side made no progress.
 */
class GiveUp
{
  public:
    bool Now()
    {
        if (std::chrono::steady_clock::now() > deadline) {
            gave_up.store(true);
        }
        return gave_up.load();
    }

    bool Did() const
    {
        return gave_up.load();
    }

  private:
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::atomic<bool> gave_up{false};
};

struct StressCase {
    size_t capacity;
    size_t write_chunk;
    size_t read_chunk;
};

class RingStress : public testing::TestWithParam<StressCase>
{
};

/* The plain functions: the producer writes what fits and comes back with
 * the rest, the consumer reads until it has every byte. */
TEST_P(RingStress, BytesCrossBetweenTwoThreadsWholeAndInOrder)
{
    constexpr size_t kTotal = 1000000;
    const StressCase c = GetParam();
    std::vector<uint8_t> storage(c.capacity);
    el_ring ring;
    el_ring_init(&ring, storage.data(), storage.size());

    GiveUp give_up;
    std::thread producer([&ring, &give_up, c] {
        std::vector<uint8_t> chunk(c.write_chunk);
        size_t sent = 0;
        while (sent < kTotal) {
            const size_t len = c.write_chunk < kTotal - sent ? c.write_chunk : kTotal - sent;
            for (size_t i = 0; i < len; i++) {
                chunk[i] = ByteAt(sent + i);
            }
            size_t done = 0;
            while (done < len) {
                const size_t n = el_ring_write(&ring, chunk.data() + done, len - done);
                if (n == 0) {
                    /* Full: the consumer is behind. */
                    if (give_up.Now()) {
                        return;
                    }
                    std::this_thread::yield();
                }
                done += n;
            }
            sent += len;
        }
    });

    std::vector<uint8_t> chunk(c.read_chunk);
    size_t received = 0;
    size_t wrong = 0;
    while (received < kTotal) {
        const size_t n = el_ring_read(&ring, chunk.data(), chunk.size());
        if (n == 0) {
            if (give_up.Now()) {
                break;
            }
            std::this_thread::yield();
            continue;
        }
        for (size_t i = 0; i < n; i++) {
            wrong += chunk[i] != ByteAt(received + i) ? 1u : 0u;
        }
        received += n;
    }
    producer.join();

    EXPECT_FALSE(give_up.Did()) << "the ring stopped moving bytes, after " << received;
    EXPECT_EQ(wrong, 0u) << "bytes arrived changed or out of order";
    EXPECT_EQ(received, kTotal);
    EXPECT_EQ(el_ring_len(&ring), 0u);
}

/* The two sides as streams, and the close: the consumer learns from the
 * reader alone that the producer has finished, after the last byte. */
TEST_P(RingStress, TheReaderEndsAfterTheLastByteTheOtherThreadWrote)
{
    constexpr size_t kTotal = 200000;
    const StressCase c = GetParam();
    std::vector<uint8_t> storage(c.capacity);
    el_ring ring;
    el_ring_init(&ring, storage.data(), storage.size());

    GiveUp give_up;
    std::atomic<bool> producer_done{false};
    std::thread producer([&ring, &give_up, &producer_done, c] {
        const el_writer out = el_ring_as_writer(&ring);
        std::vector<uint8_t> chunk(c.write_chunk);
        size_t sent = 0;
        while (sent < kTotal) {
            const size_t len = c.write_chunk < kTotal - sent ? c.write_chunk : kTotal - sent;
            for (size_t i = 0; i < len; i++) {
                chunk[i] = ByteAt(sent + i);
            }
            size_t done = 0;
            while (done < len) {
                size_t n = 0;
                const el_status status = el_write_all(out, chunk.data() + done, len - done, &n);
                done += n;
                if (status != EL_OK) {
                    /* Full, EL_ERR_WOULD_BLOCK: the consumer is behind. A
                     * status that never clears ends at the deadline. */
                    if (give_up.Now()) {
                        producer_done.store(true);
                        return;
                    }
                    std::this_thread::yield();
                }
            }
            sent += len;
        }
        el_ring_close(&ring);
        producer_done.store(true);
    });

    const el_reader in = el_ring_as_reader(&ring);
    std::vector<uint8_t> chunk(c.read_chunk);
    size_t received = 0;
    size_t wrong = 0;
    el_status status;
    for (;;) {
        size_t n = 0;
        status = el_read(in, chunk.data(), chunk.size(), &n);
        if (status == EL_ERR_WOULD_BLOCK) {
            if (give_up.Now()) {
                break;
            }
            std::this_thread::yield();
            continue;
        }
        if (status != EL_OK) {
            break;
        }
        for (size_t i = 0; i < n; i++) {
            wrong += chunk[i] != ByteAt(received + i) ? 1u : 0u;
        }
        received += n;
        if (received > kTotal) {
            break; /* more than was written: a broken ring may never stop */
        }
    }
    /* A reader that ended early would leave the producer facing a full
     * ring: take what it still writes, so that the test fails and does not
     * wait for the deadline. */
    while (!producer_done.load()) {
        uint8_t rest[256];
        (void)el_ring_read(&ring, rest, sizeof(rest));
        std::this_thread::yield();
    }
    producer.join();

    EXPECT_FALSE(give_up.Did()) << "the ring stopped moving bytes, after " << received;
    EXPECT_EQ(status, EL_ERR_END_OF_STREAM) << el_status_name(status);
    EXPECT_EQ(received, kTotal) << "the reader ended before the last byte, or after more";
    EXPECT_EQ(wrong, 0u) << "bytes arrived changed or out of order";
}

INSTANTIATE_TEST_SUITE_P(Capacities, RingStress,
                         testing::Values(StressCase{1, 1, 1}, StressCase{7, 5, 3},
                                         StressCase{64, 100, 9}, StressCase{4096, 300, 4096}),
                         [](const testing::TestParamInfo<StressCase> &param_info) {
                             return "Capacity" + std::to_string(param_info.param.capacity);
                         });

/*
 * The close, many times over. The reader spins on an empty ring while the
 * other thread writes its last bytes and closes, and must still read every
 * one of them before it ends. A reader that looked at the close after
 * finding the ring empty, not before, would end early when both of the
 * producer's stores land between its two loads: a window a few
 * instructions wide, which each round gives one more chance to hit.
 */
TEST(RingStressClose, TheLastBytesBeforeACloseAreAlwaysRead)
{
    constexpr unsigned kRounds = 20000;
    uint8_t storage[8];
    el_ring ring;
    std::atomic<unsigned> started{0};
    GiveUp give_up;

    std::thread producer([&ring, &started, &give_up] {
        for (unsigned round = 1; round <= kRounds; round++) {
            /* Spin: the reader is spinning too, so on two cores the two
             * run at once. Yield now and then, for a machine with one. */
            for (unsigned spins = 1; started.load() != round; spins++) {
                if (spins % 1024 == 0) {
                    if (give_up.Now()) {
                        return;
                    }
                    std::this_thread::yield();
                }
            }
            const uint8_t bytes[3] = {static_cast<uint8_t>(round), static_cast<uint8_t>(round),
                                      static_cast<uint8_t>(round)};
            (void)el_ring_write(&ring, bytes, round % 3 + 1);
            el_ring_close(&ring);
        }
    });

    unsigned early = 0;
    unsigned wrong = 0;
    const el_reader in = el_ring_as_reader(&ring);
    for (unsigned round = 1; round <= kRounds && !give_up.Did(); round++) {
        /* The producer is done with the ring: it closed it last round. */
        el_ring_init(&ring, storage, sizeof(storage));
        started.store(round);

        size_t received = 0;
        for (unsigned spins = 0;; spins++) {
            uint8_t bytes[8];
            size_t n = 0;
            const el_status status = el_read(in, bytes, sizeof(bytes), &n);
            if (status == EL_OK) {
                for (size_t i = 0; i < n; i++) {
                    wrong += bytes[i] != static_cast<uint8_t>(round) ? 1u : 0u;
                }
                received += n;
                if (received > 3) {
                    break; /* more than was written */
                }
            } else if (status != EL_ERR_WOULD_BLOCK) {
                break; /* the end */
            } else if (spins % 1024 == 1023) {
                if (give_up.Now()) {
                    break;
                }
                std::this_thread::yield();
            }
        }
        early += received < round % 3 + 1 ? 1u : 0u;
        wrong += received > round % 3 + 1 ? 1u : 0u;
    }
    producer.join();

    EXPECT_FALSE(give_up.Did()) << "the ring stopped moving bytes";
    EXPECT_EQ(early, 0u) << "of " << kRounds << " rounds, the reader ended before the last bytes";
    EXPECT_EQ(wrong, 0u);
}

} // namespace
