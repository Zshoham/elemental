/*
 * Contract fixtures for the two arenas. Plain C, like the arenas.
 *
 * Both can be bounded: the bound is the size of the storage. They differ in
 * what freeing gives back. The bump arena gives back nothing until a reset.
 * The stack arena gives back the latest allocation, but not in general an
 * allocation freed out of order, so it claims RECLAIMS_LATEST and not ANY.
 */
#include "fixture_arena.h"

#include <elemental/alloc_arena.h>

#include <stdalign.h>

/*
 * The working set's sizes, plus the worst-case padding for each of its
 * allocations. The bounded arenas get exactly their limit, so they cannot
 * hold more than it.
 */
#define WORKING_STORAGE \
    (EL_ALLOC_CONTRACT_SIZE_MAX + EL_ALLOC_CONTRACT_COUNT_MAX * EL_ALLOC_CONTRACT_ALIGN_MAX)

static alignas(EL_ALLOC_CONTRACT_ALIGN_MAX) unsigned char storage[WORKING_STORAGE];
static el_bump_arena bump;
static el_stack_arena stack;

static void reset(void *ctx)
{
    (void)ctx;
    el_bump_arena_init(&bump, NULL, 0);
    el_stack_arena_init(&stack, NULL, 0);
}

static el_allocator bump_allocator(void *ctx)
{
    (void)ctx;
    el_bump_arena_init(&bump, storage, sizeof(storage));
    return el_bump_arena_as_allocator(&bump);
}

static el_allocator bump_bounded(void *ctx, size_t limit)
{
    (void)ctx;
    el_bump_arena_init(&bump, storage, limit);
    return el_bump_arena_as_allocator(&bump);
}

static el_allocator stack_allocator(void *ctx)
{
    (void)ctx;
    el_stack_arena_init(&stack, storage, sizeof(storage));
    return el_stack_arena_as_allocator(&stack);
}

static el_allocator stack_bounded(void *ctx, size_t limit)
{
    (void)ctx;
    el_stack_arena_init(&stack, storage, limit);
    return el_stack_arena_as_allocator(&stack);
}

const el_alloc_contract_fixture alloc_fixture_bump_arena = {
    .name = "bump_arena",
    .ctx = NULL,
    .reset = reset,
    .allocator = bump_allocator,
    .allocator_bounded = bump_bounded,
    .reclaims = EL_ALLOC_CONTRACT_RECLAIMS_NOTHING,
};

const el_alloc_contract_fixture alloc_fixture_stack_arena = {
    .name = "stack_arena",
    .ctx = NULL,
    .reset = reset,
    .allocator = stack_allocator,
    .allocator_bounded = stack_bounded,
    .reclaims = EL_ALLOC_CONTRACT_RECLAIMS_LATEST,
};
