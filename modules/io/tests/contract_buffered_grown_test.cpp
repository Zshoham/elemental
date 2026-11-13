/* The io contract suite against the buffered writer, grown storage. */
#include <elemental/io_contract.hpp>

#include "fixture_buffered.h"

INSTANTIATE_TEST_SUITE_P(IoBufferedGrown, ElIoContract,
                         testing::Values(&io_alloc_fixture_buffered_grown));
