/* The clock contract suite against the system's monotonic clock. */
#include <elemental/time_contract.hpp>

#include "fixture_posix.h"

INSTANTIATE_TEST_SUITE_P(Posix, ElTimeContract, testing::Values(&time_fixture_posix));
