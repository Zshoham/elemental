/* The allocator contract suite against both arenas. */
#include <elemental/alloc_contract.hpp>

#include "fixture_arena.h"

INSTANTIATE_TEST_SUITE_P(
    Arena, ElAllocContract, testing::Values(&alloc_fixture_bump_arena, &alloc_fixture_stack_arena),
    [](const testing::TestParamInfo<const el_alloc_contract_fixture *> &param_info) {
        return std::string(param_info.param->name);
    });
