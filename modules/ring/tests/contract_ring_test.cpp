/* The io contract suite against the two sides of an el_ring. */
#include <elemental/io_contract.hpp>

#include "fixture_ring.h"

INSTANTIATE_TEST_SUITE_P(Ring, ElIoContract, testing::Values(&ring_fixture, &ring_fixture_open));
