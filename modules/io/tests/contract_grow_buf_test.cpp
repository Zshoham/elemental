/* The io contract suite against an el_grow_buf. */
#include <elemental/io_contract.hpp>

#include "fixture_grow_buf.h"

INSTANTIATE_TEST_SUITE_P(IoGrowBuf, ElIoContract, testing::Values(&io_alloc_fixture_grow_buf));
