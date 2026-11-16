/*
 * What only the Zephyr clock promises (elemental/time_zephyr.h): which
 * clock it is, its unit, and that the handle is stateless.
 */
#include <gtest/gtest.h>

#include <elemental/time_zephyr.h>

#include <cstdint>

#include <zephyr/kernel.h>

TEST(TimeZephyr, ATickIsAKernelTick)
{
    EXPECT_EQ(el_clock_ticks_per_second(el_zephyr_clock()),
              static_cast<uint32_t>(CONFIG_SYS_CLOCK_TICKS_PER_SEC));
}

TEST(TimeZephyr, ReadsTheKernelsUptime)
{
    /* Its reading lies between two readings of the uptime taken around it. */
    for (int i = 0; i < 100; i++) {
        const int64_t before = k_uptime_ticks();
        const el_ticks reading = el_clock_now(el_zephyr_clock());
        const int64_t after = k_uptime_ticks();
        ASSERT_GE(reading, static_cast<el_ticks>(before));
        ASSERT_LE(reading, static_cast<el_ticks>(after));
    }
}

TEST(TimeZephyr, ATenthOfASecondOfSleepIsATenthOfASecondOfTicks)
{
    const el_clock clock = el_zephyr_clock();
    const el_ticks start = el_clock_now(clock);
    k_sleep(K_MSEC(100));
    const el_ticks took = el_clock_now(clock) - start;

    /* At least what was slept, and not wildly more: the unit is right. */
    const el_ticks tenth = el_clock_ticks_per_second(clock) / 10u;
    EXPECT_GE(took, tenth);
    EXPECT_LT(took, tenth * 3u);
}

TEST(TimeZephyr, TheHandleHasNoStateAndEveryHandleReadsTheSameClock)
{
    const el_clock first = el_zephyr_clock();
    const el_clock second = el_zephyr_clock();

    EXPECT_EQ(first.ctx, nullptr);
    EXPECT_EQ(first.ops, second.ops);
    const el_ticks a = el_clock_now(first);
    const el_ticks b = el_clock_now(second);
    EXPECT_LE(a, b);
}
