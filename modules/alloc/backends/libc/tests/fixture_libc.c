/*
 * Contract fixture for the C library heap. The heap has no bound this
 * backend can set, so the bounded capability, and with it every reclaim
 * test, is absent: freeing does give memory back, but without a bound the
 * suite cannot observe it.
 */
#include "fixture_libc.h"

#include <elemental/alloc_libc.h>

static void reset(void *ctx)
{
    (void)ctx; /* Nothing to reset: the suite frees what it allocates. */
}

static el_allocator allocator(void *ctx)
{
    (void)ctx;
    return el_libc_allocator();
}

const el_alloc_contract_fixture alloc_fixture_libc = {
    .name = "libc",
    .ctx = NULL,
    .reset = reset,
    .allocator = allocator,
    .allocator_bounded = NULL,
    .reclaims = EL_ALLOC_CONTRACT_RECLAIMS_NOTHING,
};
