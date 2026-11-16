/* What only the step clock promises (step_clock.h). */
#include <gtest/gtest.h>

#include <cstdint>

#include "step_clock.h"

TEST(StepClock, TheFirstReadingIsZeroAndEachIsOneStepLater)
{
    step_clock stepping;
    step_clock_init(&stepping, 1000, 250);
    const el_clock clock = step_clock_as_clock(&stepping);

    EXPECT_EQ(el_clock_now(clock), 0u);
    EXPECT_EQ(el_clock_now(clock), 250u);
    EXPECT_EQ(el_clock_now(clock), 500u);
}

TEST(StepClock, StatesTheFrequencyItWasGiven)
{
    step_clock stepping;
    step_clock_init(&stepping, 32768, 1);

    EXPECT_EQ(el_clock_ticks_per_second(step_clock_as_clock(&stepping)), 32768u);
}

TEST(StepClock, AStepOfZeroStandsStill)
{
    step_clock stepping;
    step_clock_init(&stepping, 1000, 0);
    const el_clock clock = step_clock_as_clock(&stepping);

    EXPECT_EQ(el_clock_now(clock), 0u);
    EXPECT_EQ(el_clock_now(clock), 0u);
}

TEST(StepClock, StopsAtTheLargestReadingInsteadOfGoingBack)
{
    step_clock stepping;
    step_clock_init(&stepping, 1, UINT64_MAX - 1u);
    const el_clock clock = step_clock_as_clock(&stepping);

    EXPECT_EQ(el_clock_now(clock), 0u);
    EXPECT_EQ(el_clock_now(clock), UINT64_MAX - 1u);
    EXPECT_EQ(el_clock_now(clock), UINT64_MAX);
    EXPECT_EQ(el_clock_now(clock), UINT64_MAX);
}
