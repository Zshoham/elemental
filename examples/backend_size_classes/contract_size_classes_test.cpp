/*
 * The allocator contract suite against the size_classes example. Besides
 * the fixture (size_classes_fixture.c), this line is all an allocator
 * author writes to run every promise of the interface against theirs.
 */
#include <elemental/alloc_contract.hpp>

#include "size_classes_fixture.h"

INSTANTIATE_TEST_SUITE_P(SizeClasses, ElAllocContract, testing::Values(&size_classes_fixture));
