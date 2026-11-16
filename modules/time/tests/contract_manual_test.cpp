/* The clock contract suite against the manual clock. */
#include <elemental/time_contract.hpp>

#include "fixture_manual.h"

INSTANTIATE_TEST_SUITE_P(Manual, ElTimeContract, testing::Values(&time_fixture_manual));
