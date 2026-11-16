/*
 * The front functions, the conversions, and the manual clock: what
 * elemental/time.h and elemental/time_manual.h promise beyond the
 * interface the contract suite covers.
 */
#include <gtest/gtest.h>

#include <elemental/time.h>
#include <elemental/time_manual.h>

#include <cstdint>

#include "scripted_clock.h"

namespace
{

constexpr uint32_t kRateMax = UINT32_MAX;

/* Frequencies from the slowest to the fastest a clock may state. */
constexpr uint32_t kRates[] = {1,     2,       3,        100,       1000,       1024,    10000,
                               32768, 1000000, 16000000, 999999937, 1000000000, kRateMax};

/* Random values tried per frequency. The emulated boards run them too. */
constexpr int kRounds = 400;

/* xorshift64: the same values on every target, and no C library. */
struct Random {
    uint64_t state = 88172645463325252u;
    uint64_t Next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
};

/* The same results by the compiler's own 64-bit arithmetic. Tests may use
 * it; the library does not (src/time_convert.c). */
uint64_t FromMsByDivision(uint32_t ms, uint32_t rate)
{
    return (static_cast<uint64_t>(ms) * rate + 999u) / 1000u;
}

uint32_t MicrosecondsByDivision(uint64_t ticks, uint32_t rate)
{
    return static_cast<uint32_t>((ticks % rate) * 1000000u / rate);
}

} // namespace

/* ---- The front functions ------------------------------------------------- */

TEST(ElClock, NowReturnsTheImplementationsReadingAndPassesItsContext)
{
    scripted_clock scripted = {};
    scripted.reading = 0x0123456789ABCDEFu;
    scripted.ticks_per_second = 1000;

    EXPECT_EQ(el_clock_now(scripted_clock_as_clock(&scripted)), 0x0123456789ABCDEFu);
    EXPECT_EQ(scripted.now_calls, 1u);
    EXPECT_EQ(scripted.ticks_per_second_calls, 0u);
    EXPECT_EQ(scripted.last_ctx, &scripted);
}

TEST(ElClock, TicksPerSecondReturnsTheImplementationsAndPassesItsContext)
{
    scripted_clock scripted = {};
    scripted.ticks_per_second = 32768;

    EXPECT_EQ(el_clock_ticks_per_second(scripted_clock_as_clock(&scripted)), 32768u);
    EXPECT_EQ(scripted.ticks_per_second_calls, 1u);
    EXPECT_EQ(scripted.now_calls, 0u);
    EXPECT_EQ(scripted.last_ctx, &scripted);
}

TEST(ElClock, TheLargestReadingAndFrequencyPassThrough)
{
    scripted_clock scripted = {};
    scripted.reading = UINT64_MAX;
    scripted.ticks_per_second = kRateMax;

    EXPECT_EQ(el_clock_now(scripted_clock_as_clock(&scripted)), UINT64_MAX);
    EXPECT_EQ(el_clock_ticks_per_second(scripted_clock_as_clock(&scripted)), kRateMax);
}

/* ---- Milliseconds to ticks ----------------------------------------------- */

TEST(ElTicks, FromMsIsExactWhenTheMillisecondsAreWholeTicks)
{
    EXPECT_EQ(el_ticks_from_ms(0, 1000), 0u);
    EXPECT_EQ(el_ticks_from_ms(1, 1000), 1u);
    EXPECT_EQ(el_ticks_from_ms(250, 1000), 250u);
    EXPECT_EQ(el_ticks_from_ms(1000, 32768), 32768u);
    EXPECT_EQ(el_ticks_from_ms(1000, 1), 1u);
    EXPECT_EQ(el_ticks_from_ms(3, 1000000000), 3000000u);
    EXPECT_EQ(el_ticks_from_ms(UINT32_MAX, 1000), UINT32_MAX);
}

TEST(ElTicks, FromMsRoundsUpSoThatATimeoutNeverEndsEarly)
{
    /* A tenth of a tick, and anything short of a whole one, is one tick. */
    EXPECT_EQ(el_ticks_from_ms(1, 100), 1u);
    EXPECT_EQ(el_ticks_from_ms(1, 1), 1u);
    EXPECT_EQ(el_ticks_from_ms(999, 1), 1u);
    EXPECT_EQ(el_ticks_from_ms(1001, 1), 2u);
    EXPECT_EQ(el_ticks_from_ms(15, 100), 2u);
    /* 1 ms at 32768 Hz is 32.768 ticks. */
    EXPECT_EQ(el_ticks_from_ms(1, 32768), 33u);
}

TEST(ElTicks, FromMsOfNoTimeIsNoTicksAtEveryFrequency)
{
    for (const uint32_t rate : kRates) {
        EXPECT_EQ(el_ticks_from_ms(0, rate), 0u) << rate << " ticks per second";
    }
}

TEST(ElTicks, FromMsDoesNotOverflowAtTheLargestArguments)
{
    /* (2^32 - 1)^2 / 1000, rounded up. */
    EXPECT_EQ(el_ticks_from_ms(UINT32_MAX, kRateMax), 18446744065119618u);
    EXPECT_EQ(el_ticks_from_ms(UINT32_MAX, kRateMax), FromMsByDivision(UINT32_MAX, kRateMax));
}

TEST(ElTicks, FromMsAgreesWithDivision)
{
    Random random;
    for (const uint32_t rate : kRates) {
        for (int i = 0; i < kRounds; i++) {
            const uint64_t bits = random.Next();
            const uint32_t ms = static_cast<uint32_t>(bits >> (random.Next() % 33));
            ASSERT_EQ(el_ticks_from_ms(ms, rate), FromMsByDivision(ms, rate))
                << ms << " ms at " << rate << " ticks per second";
        }
    }
}

/* ---- Ticks to seconds ---------------------------------------------------- */

TEST(ElTicks, ToSecondsSplitsWholeSecondsFromMicroseconds)
{
    uint32_t us = 123;
    EXPECT_EQ(el_ticks_to_seconds(0, 1000, &us), 0u);
    EXPECT_EQ(us, 0u);

    EXPECT_EQ(el_ticks_to_seconds(999, 1000, &us), 0u);
    EXPECT_EQ(us, 999000u);

    EXPECT_EQ(el_ticks_to_seconds(1000, 1000, &us), 1u);
    EXPECT_EQ(us, 0u);

    EXPECT_EQ(el_ticks_to_seconds(12034, 1000, &us), 12u);
    EXPECT_EQ(us, 34000u);

    EXPECT_EQ(el_ticks_to_seconds(7509, 10000, &us), 0u);
    EXPECT_EQ(us, 750900u);

    EXPECT_EQ(el_ticks_to_seconds(3000000017u, 1000000000, &us), 3u);
    EXPECT_EQ(us, 0u);
}

TEST(ElTicks, ToSecondsRoundsMicrosecondsDown)
{
    uint32_t us;
    /* A third of a second is 333333.3 microseconds. */
    EXPECT_EQ(el_ticks_to_seconds(1, 3, &us), 0u);
    EXPECT_EQ(us, 333333u);
    /* The last tick before a second never rounds up into it. */
    EXPECT_EQ(el_ticks_to_seconds(kRateMax - 1u, kRateMax, &us), 0u);
    EXPECT_EQ(us, 999999u);
    /* 999 nanoseconds is no whole microsecond. */
    EXPECT_EQ(el_ticks_to_seconds(999, 1000000000, &us), 0u);
    EXPECT_EQ(us, 0u);
}

TEST(ElTicks, ToSecondsHandlesTheLargestCountAtEveryFrequency)
{
    for (const uint32_t rate : kRates) {
        uint32_t us = UINT32_MAX;
        EXPECT_EQ(el_ticks_to_seconds(UINT64_MAX, rate, &us), UINT64_MAX / rate)
            << rate << " ticks per second";
        EXPECT_EQ(us, MicrosecondsByDivision(UINT64_MAX, rate)) << rate << " ticks per second";
        EXPECT_LT(us, 1000000u);
    }
}

TEST(ElTicks, ToSecondsAgreesWithDivision)
{
    Random random;
    for (const uint32_t rate : kRates) {
        for (int i = 0; i < kRounds; i++) {
            /* Every magnitude, and the values around a whole second. */
            const uint64_t bits = random.Next();
            uint64_t ticks = bits >> (random.Next() % 64);
            if (i < 3) {
                ticks = static_cast<uint64_t>(rate) + static_cast<uint64_t>(i) - 1u;
            }
            uint32_t us = UINT32_MAX;
            ASSERT_EQ(el_ticks_to_seconds(ticks, rate, &us), ticks / rate)
                << ticks << " ticks at " << rate << " per second";
            ASSERT_EQ(us, MicrosecondsByDivision(ticks, rate))
                << ticks << " ticks at " << rate << " per second";
        }
    }
}

TEST(ElTicks, ConvertingMillisecondsToTicksAndBackLosesLessThanATick)
{
    for (const uint32_t rate : kRates) {
        uint32_t us;
        const uint64_t seconds = el_ticks_to_seconds(el_ticks_from_ms(2500, rate), rate, &us);
        if (rate >= 2) {
            /* Rounded up by less than one tick: 2.5 s, plus under 1/rate s. */
            EXPECT_EQ(seconds, 2u) << rate << " ticks per second";
            EXPECT_GE(us, 500000u) << rate << " ticks per second";
            EXPECT_LE(us, 500000u + 1000000u / rate) << rate << " ticks per second";
        } else {
            EXPECT_EQ(seconds, 3u);
            EXPECT_EQ(us, 0u);
        }
    }
}

/* ---- The manual clock ---------------------------------------------------- */

TEST(ElManualClock, ReadsZeroUntilItIsAdvanced)
{
    el_manual_clock manual;
    el_manual_clock_init(&manual, 1000);
    const el_clock clock = el_manual_clock_as_clock(&manual);

    EXPECT_EQ(el_clock_now(clock), 0u);
    EXPECT_EQ(el_clock_now(clock), 0u);
}

TEST(ElManualClock, StatesTheFrequencyItWasGiven)
{
    for (const uint32_t rate : kRates) {
        el_manual_clock manual;
        el_manual_clock_init(&manual, rate);
        EXPECT_EQ(el_clock_ticks_per_second(el_manual_clock_as_clock(&manual)), rate);
    }
}

TEST(ElManualClock, AdvancingMovesTheReadingByExactlyThatMuch)
{
    el_manual_clock manual;
    el_manual_clock_init(&manual, 1000);
    const el_clock clock = el_manual_clock_as_clock(&manual);

    el_manual_clock_advance(&manual, 250);
    EXPECT_EQ(el_clock_now(clock), 250u);
    el_manual_clock_advance(&manual, 0);
    EXPECT_EQ(el_clock_now(clock), 250u);
    el_manual_clock_advance(&manual, 1);
    EXPECT_EQ(el_clock_now(clock), 251u);
    /* A handle made later reads the same clock. */
    EXPECT_EQ(el_clock_now(el_manual_clock_as_clock(&manual)), 251u);
}

TEST(ElManualClock, CanBeAdvancedToTheLargestReading)
{
    el_manual_clock manual;
    el_manual_clock_init(&manual, 1);
    el_manual_clock_advance(&manual, 7);
    el_manual_clock_advance(&manual, UINT64_MAX - 7u);

    EXPECT_EQ(el_clock_now(el_manual_clock_as_clock(&manual)), UINT64_MAX);
}

TEST(ElManualClock, InitializingAgainStartsOverAtZero)
{
    el_manual_clock manual;
    el_manual_clock_init(&manual, 1000);
    el_manual_clock_advance(&manual, 99);
    el_manual_clock_init(&manual, 10);

    EXPECT_EQ(el_clock_now(el_manual_clock_as_clock(&manual)), 0u);
    EXPECT_EQ(el_clock_ticks_per_second(el_manual_clock_as_clock(&manual)), 10u);
}
