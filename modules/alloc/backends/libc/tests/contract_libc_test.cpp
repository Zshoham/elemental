/* The allocator contract suite against the C library heap. */
#include <elemental/alloc_contract.hpp>

#include "fixture_libc.h"

INSTANTIATE_TEST_SUITE_P(Libc, ElAllocContract, testing::Values(&alloc_fixture_libc));
