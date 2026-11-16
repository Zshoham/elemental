/*
 * The log sink contract suite against the tee sink example. Besides the
 * fixture (tee_sink_fixture.c), this line is all a sink's author writes to
 * run every promise of the interface against theirs.
 */
#include <elemental/log_contract.hpp>

#include "tee_sink_fixture.h"

INSTANTIATE_TEST_SUITE_P(TeeSink, ElLogContract, testing::Values(&tee_sink_fixture));
