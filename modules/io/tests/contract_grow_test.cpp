/* The io contract suite against the grow writer. */
#include <elemental/io_contract.hpp>

#include "fixture_grow.h"

INSTANTIATE_TEST_SUITE_P(IoAllocGrow, ElIoContract, testing::Values(&io_alloc_fixture_grow));
