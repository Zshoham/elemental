/* The io contract suite against the buffered writer, fixed storage. */
#include <elemental/io_contract.hpp>

#include "fixture_buffered.h"

INSTANTIATE_TEST_SUITE_P(IoBuffered, ElIoContract,
                         testing::Values(&io_fixture_buffered_fixed,
                                         &io_fixture_buffered_pass_through));
