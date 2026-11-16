/*
 * The clock contract suite against the step clock example. Besides the
 * fixture (step_clock_fixture.c), this line is all a clock's author writes
 * to run every promise of the interface against theirs.
 */
#include <elemental/time_contract.hpp>

#include "step_clock_fixture.h"

INSTANTIATE_TEST_SUITE_P(StepClock, ElTimeContract, testing::Values(&step_clock_fixture));
