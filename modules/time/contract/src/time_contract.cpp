/*
 * The clock contract as tests. Each test names one promise made in
 * elemental/time.h. This file uses nothing but the time module and
 * GoogleTest.
 */
#include <elemental/time_contract.hpp>

#include <cstdint>
#include <ostream>

namespace
{

/* Readings taken in a row by the tests that look for a step backwards. */
constexpr int kReadings = 1000;

/* Times the tests ask for time to pass. */
constexpr int kSteps = 5;

el_clock Open(const el_time_contract_fixture *fixture)
{
    if (fixture->reset != nullptr) {
        fixture->reset(fixture->ctx);
    }
    return fixture->clock(fixture->ctx);
}

/* About a millisecond of this clock's ticks, and never none. */
el_ticks Step(el_clock clock)
{
    const el_ticks step = el_clock_ticks_per_second(clock) / 1000u;
    return step == 0 ? 1 : step;
}

} // namespace

void ElTimeContract::SetUp()
{
    const el_time_contract_fixture *const fixture = GetParam();
    ASSERT_TRUE(fixture != nullptr) << "the suite was instantiated with a null fixture";
    ASSERT_TRUE(fixture->clock != nullptr) << "fixture.clock is required";
}

void ElTimeContract::TearDown()
{
    const el_time_contract_fixture *const fixture = GetParam();
    if (fixture != nullptr && fixture->reset != nullptr) {
        fixture->reset(fixture->ctx);
    }
}

void PrintTo(const el_time_contract_fixture *fixture, std::ostream *os)
{
    if (fixture == nullptr) {
        *os << "(null fixture)";
    } else if (fixture->name == nullptr) {
        *os << "(unnamed fixture)";
    } else {
        *os << fixture->name;
    }
}

TEST_P(ElTimeContract, TicksPerSecondIsAtLeastOne)
{
    const el_clock clock = Open(GetParam());

    /* A clock that says 0 is also stopped by the front function's check,
     * where assertions are compiled in. */
    EXPECT_GE(clock.ops->ticks_per_second(clock.ctx), 1u);
}

TEST_P(ElTimeContract, TicksPerSecondIsTheSameOnEveryCall)
{
    const el_clock clock = Open(GetParam());

    const uint32_t first = el_clock_ticks_per_second(clock);
    for (int i = 0; i < kReadings; i++) {
        /* Reading the clock in between must not change what a tick is. */
        (void)el_clock_now(clock);
        ASSERT_EQ(el_clock_ticks_per_second(clock), first) << "after " << i << " readings";
    }
}

TEST_P(ElTimeContract, ReadingsNeverGoBack)
{
    const el_clock clock = Open(GetParam());

    el_ticks last = el_clock_now(clock);
    for (int i = 0; i < kReadings; i++) {
        const el_ticks now = el_clock_now(clock);
        ASSERT_GE(now, last) << "reading " << i << " is earlier than the one before it";
        last = now;
    }
}

TEST_P(ElTimeContract, AReadingGrowsByAtLeastTheTimeThatPassed)
{
    const el_time_contract_fixture *const fixture = GetParam();
    if (fixture->advance == nullptr) {
        GTEST_SKIP() << "the fixture has no advance: time cannot be made to pass on this clock";
    }
    const el_clock clock = Open(fixture);
    const el_ticks step = Step(clock);

    for (int i = 0; i < kSteps; i++) {
        const el_ticks before = el_clock_now(clock);
        fixture->advance(fixture->ctx, step);
        const el_ticks after = el_clock_now(clock);
        ASSERT_GE(after, before) << "step " << i;
        EXPECT_GE(after - before, step) << "step " << i << ": " << step << " ticks passed";
    }
}

TEST_P(ElTimeContract, ReadingsNeverGoBackWhileTimePasses)
{
    const el_time_contract_fixture *const fixture = GetParam();
    if (fixture->advance == nullptr) {
        GTEST_SKIP() << "the fixture has no advance: time cannot be made to pass on this clock";
    }
    const el_clock clock = Open(fixture);
    const uint32_t ticks_per_second = el_clock_ticks_per_second(clock);

    el_ticks last = el_clock_now(clock);
    for (int i = 0; i < kSteps; i++) {
        fixture->advance(fixture->ctx, 1);
        for (int j = 0; j < 10; j++) {
            const el_ticks now = el_clock_now(clock);
            ASSERT_GE(now, last) << "step " << i << ", reading " << j;
            last = now;
        }
        ASSERT_EQ(el_clock_ticks_per_second(clock), ticks_per_second) << "step " << i;
    }
}
