/*
 * The io contract suite against the POSIX backend, over pipes, once with
 * blocking descriptors and once with non-blocking ones.
 */
#include <elemental/io_contract.hpp>

#include "fixture_pipe.h"

INSTANTIATE_TEST_SUITE_P(IoPosixBlockingPipe, ElIoContract,
                         testing::Values(&io_posix_fixture_blocking_pipe));
INSTANTIATE_TEST_SUITE_P(IoPosixNonBlockingPipe, ElIoContract,
                         testing::Values(&io_posix_fixture_nonblocking_pipe));
