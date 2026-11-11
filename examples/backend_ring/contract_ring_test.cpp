/*
 * The io contract suite against the ring_pipe example backend. Besides the
 * fixture (ring_pipe_fixture.c), this line is all a backend author writes
 * to run every promise of the io interface against their implementation.
 */
#include <elemental/io_contract.hpp>

#include "ring_pipe_fixture.h"

INSTANTIATE_TEST_SUITE_P(RingPipe, ElIoContract, testing::Values(&ring_pipe_fixture));
