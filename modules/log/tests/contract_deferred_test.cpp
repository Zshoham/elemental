/* The log sink contract suite against the deferred sink. */
#include <elemental/log_contract.hpp>

#include <string>

#include "fixture_deferred.h"

INSTANTIATE_TEST_SUITE_P(
    DeferredSink, ElLogContract, testing::Values(&log_fixture_deferred),
    [](const testing::TestParamInfo<const el_log_contract_fixture *> &param_info) {
        return std::string(param_info.param->name);
    });
