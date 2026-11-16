/* The clock contract suite against the Zephyr kernel's uptime. */
#include <elemental/time_contract.hpp>

#include "fixture_zephyr.h"

INSTANTIATE_TEST_SUITE_P(Zephyr, ElTimeContract, testing::Values(&time_fixture_zephyr));
