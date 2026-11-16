/*
 * What only the POSIX clock promises (elemental/time_posix.h): which clock
 * it is, its unit, and that the handle is stateless.
 */
#include <gtest/gtest.h>

#include <elemental/time_posix.h>

#include <cstdint>

#include <time.h>

namespace
{

uint64_t MonotonicNanoseconds()
{
    timespec ts;
    EXPECT_EQ(clock_gettime(CLOCK_MONOTONIC, &ts), 0);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000u + static_cast<uint64_t>(ts.tv_nsec);
}

} // namespace

TEST(TimePosix, ATickIsANanosecond)
{
    EXPECT_EQ(el_clock_ticks_per_second(el_posix_clock()), 1000000000u);
}

TEST(TimePosix, ReadsTheSystemsMonotonicClock)
{
    /* Its reading lies between two readings of CLOCK_MONOTONIC taken around
     * it, which no other clock's would. */
    for (int i = 0; i < 100; i++) {
        const uint64_t before = MonotonicNanoseconds();
        const el_ticks reading = el_clock_now(el_posix_clock());
        const uint64_t after = MonotonicNanoseconds();
        ASSERT_GE(reading, before);
        ASSERT_LE(reading, after);
    }
}

TEST(TimePosix, TheHandleHasNoStateAndEveryHandleReadsTheSameClock)
{
    const el_clock first = el_posix_clock();
    const el_clock second = el_posix_clock();

    EXPECT_EQ(first.ctx, nullptr);
    EXPECT_EQ(first.ops, second.ops);
    const el_ticks a = el_clock_now(first);
    const el_ticks b = el_clock_now(second);
    const el_ticks c = el_clock_now(first);
    EXPECT_LE(a, b);
    EXPECT_LE(b, c);
}
