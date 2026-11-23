/*
 * Contract fixtures for the two arenas, and for two allocators built on the
 * stack arena. Plain C, like the arenas.
 *
 * Both arenas can be bounded: the bound is the size of the storage. They
 * differ in what freeing gives back. The bump arena gives back nothing until
 * a reset. The stack arena gives back the latest allocation, but not in
 * general an allocation freed out of order, so it claims RECLAIMS_LATEST
 * and not ANY. So do the two built on it.
 */
#include "fixture_arena.h"

#include <elemental/alloc_arena.h>

#include <stdalign.h>
#include <stdlib.h>

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

/*
 * The stack arena behind the checking allocator: every free and resize the
 * suite makes is checked against the layout it was made with, and a block
 * left behind ends the process at the next reset. The checker has its own
 * realloc operation, which asks the arena through el_realloc().
 */
/* One record for each live allocation: the bounded tests take a 1-byte
 * allocation at a time up to the limit. Plus one spare, for a resize the
 * arena refuses: el_realloc() then allocates through the checker, which
 * ends the process if it has no free record. */
static el_alloc_check_record records[EL_ALLOC_CONTRACT_LIMIT + 2];
static el_alloc_checker checker;

static void checked_reset(void *ctx)
{
    if (checker.records != NULL && el_alloc_checker_live(&checker) != 0) {
        abort(); /* The suite left an allocation live. */
    }
    reset(ctx);
}

static el_allocator checked_over(el_allocator inner)
{
    el_alloc_checker_init(&checker, inner, records, sizeof(records) / sizeof(records[0]));
    return el_alloc_checker_as_allocator(&checker);
}

static el_allocator checked_allocator(void *ctx)
{
    return checked_over(stack_allocator(ctx));
}

static el_allocator checked_bounded(void *ctx, size_t limit)
{
    return checked_over(stack_bounded(ctx, limit));
}

const el_alloc_contract_fixture alloc_fixture_checked_stack_arena = {
    .name = "checked_stack_arena",
    .ctx = NULL,
    .reset = checked_reset,
    .allocator = checked_allocator,
    .allocator_bounded = checked_bounded,
    .reclaims = EL_ALLOC_CONTRACT_RECLAIMS_LATEST,
};

/*
 * The stack arena behind an allocator with two operations: it forwards
 * both, and leaves realloc null, so el_realloc() allocates, copies and
 * frees for it.
 */
static el_allocator forwarded;

static void *forward_alloc(void *ctx, el_layout layout)
{
    (void)ctx;
    return el_alloc(forwarded, layout);
}

static void forward_free(void *ctx, void *ptr, el_layout layout)
{
    (void)ctx;
    el_free(forwarded, ptr, layout);
}

static const el_allocator_ops forward_ops = {
    .alloc = forward_alloc,
    .free = forward_free,
};

static el_allocator forward_allocator(void *ctx)
{
    forwarded = stack_allocator(ctx);
    return (el_allocator){.ctx = NULL, .ops = &forward_ops};
}

static el_allocator forward_bounded(void *ctx, size_t limit)
{
    forwarded = stack_bounded(ctx, limit);
    return (el_allocator){.ctx = NULL, .ops = &forward_ops};
}

const el_alloc_contract_fixture alloc_fixture_stack_arena_without_realloc = {
    .name = "stack_arena_without_realloc",
    .ctx = NULL,
    .reset = reset,
    .allocator = forward_allocator,
    .allocator_bounded = forward_bounded,
    .reclaims = EL_ALLOC_CONTRACT_RECLAIMS_LATEST,
};
