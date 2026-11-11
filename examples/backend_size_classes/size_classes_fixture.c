/*
 * Contract fixture for the size_classes example. It can be bounded (the
 * bound is the storage: no class block holds more than it), and every freed
 * block is reused by its class, so it claims RECLAIMS_ANY.
 */
#include "size_classes_fixture.h"

#include "size_classes.h"

#include <stdalign.h>

/* Room for the suite's working set rounded up to classes, with the padding
 * that aligning blocks to their size costs. */
static alignas(256) unsigned char storage[16384];
static size_classes sc;

static void reset(void *ctx)
{
    (void)ctx;
    size_classes_init(&sc, NULL, 0);
}

static el_allocator allocator(void *ctx)
{
    (void)ctx;
    size_classes_init(&sc, storage, sizeof(storage));
    return size_classes_as_allocator(&sc);
}

static el_allocator allocator_bounded(void *ctx, size_t limit)
{
    (void)ctx;
    size_classes_init(&sc, storage, limit);
    return size_classes_as_allocator(&sc);
}

const el_alloc_contract_fixture size_classes_fixture = {
    .name = "size_classes (example allocator)",
    .ctx = NULL,
    .reset = reset,
    .allocator = allocator,
    .allocator_bounded = allocator_bounded,
    .reclaims = EL_ALLOC_CONTRACT_RECLAIMS_ANY,
};
