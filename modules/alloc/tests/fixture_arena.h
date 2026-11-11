#ifndef FIXTURE_ARENA_H
#define FIXTURE_ARENA_H

#include <elemental/alloc_contract.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The bump arena (elemental/alloc_arena.h) under the contract suite. */
extern const el_alloc_contract_fixture alloc_fixture_bump_arena;

/* The stack arena (elemental/alloc_arena.h) under the contract suite. */
extern const el_alloc_contract_fixture alloc_fixture_stack_arena;

#ifdef __cplusplus
}
#endif

#endif /* FIXTURE_ARENA_H */
