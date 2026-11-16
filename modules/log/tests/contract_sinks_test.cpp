/* The log sink contract suite against the memory sink and the writer sink. */
#include <elemental/log_contract.hpp>

#include <string>

#include "fixture_mem.h"
#include "fixture_writer.h"

INSTANTIATE_TEST_SUITE_P(
    Sinks, ElLogContract, testing::Values(&log_fixture_mem, &log_fixture_writer),
    [](const testing::TestParamInfo<const el_log_contract_fixture *> &param_info) {
        return std::string(param_info.param->name);
    });
