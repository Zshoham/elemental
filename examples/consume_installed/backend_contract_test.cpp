/*
 * Running the io contract suite against a backend, from outside Elemental's
 * repository. Besides the fixture, this is all a backend author writes: the
 * installed package provides the suite, and GoogleTest provides main().
 */
#include <elemental/io_contract.hpp>

#include "ring_pipe_fixture.h"

INSTANTIATE_TEST_SUITE_P(RingPipe, ElIoContract, testing::Values(&ring_pipe_fixture));
