/*
 * Running the io contract suite from outside Elemental's repository. Besides
 * the fixture, this is all a backend author writes: the installed package
 * provides the suite, and GoogleTest provides main().
 */
#include <elemental/io_contract.hpp>

#include "fixture_ring.h"

INSTANTIATE_TEST_SUITE_P(Ring, ElIoContract, testing::Values(&ring_fixture));
