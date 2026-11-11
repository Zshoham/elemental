/* The io contract suite against the memory streams. */
#include <elemental/io_contract.hpp>

#include "fixture_mem.h"

INSTANTIATE_TEST_SUITE_P(IoMem, ElIoContract, testing::Values(&io_fixture_mem));
