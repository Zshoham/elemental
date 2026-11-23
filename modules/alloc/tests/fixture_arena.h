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

/* The stack arena behind the checking allocator (elemental/alloc_contract.h). */
extern const el_alloc_contract_fixture alloc_fixture_checked_stack_arena;

/* The stack arena behind an allocator that leaves the realloc operation null. */
extern const el_alloc_contract_fixture alloc_fixture_stack_arena_without_realloc;

#ifdef __cplusplus
}
#endif

#endif /* FIXTURE_ARENA_H */
