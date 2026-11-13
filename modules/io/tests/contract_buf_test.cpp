/* The io contract suite against a fixed el_buf and the memory reader. */
#include <elemental/io_contract.hpp>

#include "fixture_buf.h"

INSTANTIATE_TEST_SUITE_P(IoBuf, ElIoContract, testing::Values(&io_fixture_buf));
