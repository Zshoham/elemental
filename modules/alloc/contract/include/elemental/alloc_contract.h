/*
 * elemental/alloc_contract.h - the fixture for the allocator contract
 * suite, and a checking allocator for tests.
 *
 * Every el_allocator implementation is expected to pass the allocator
 * contract suite (elemental/alloc_contract.hpp). The suite checks what the
 * interface promises (elemental/alloc.h), not what one allocator does;
 * that belongs in the allocator's own tests.
 *
 * Both parts are test support. They need a hosted C library and are built
 * into the test that uses them, like the rest of the suite.
 *
 * See modules/alloc/tests/fixture_arena.c for complete fixtures.
 */
#ifndef ELEMENTAL_ALLOC_CONTRACT_H
#define ELEMENTAL_ALLOC_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/alloc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Fixture ------------------------------------------------------------ */

/*
 * The suite's working set: at most this many allocations live at once,
 * totalling at most this many bytes, each aligned to at most this.
 */
#define EL_ALLOC_CONTRACT_COUNT_MAX 64u
#define EL_ALLOC_CONTRACT_SIZE_MAX 4096u
#define EL_ALLOC_CONTRACT_ALIGN_MAX 64u

/* The limit the suite passes to allocator_bounded. */
#define EL_ALLOC_CONTRACT_LIMIT 256u

/*
 * What freeing gives back, in a bounded allocator (`reclaims` below).
 * Without a bound it cannot be observed, so it is not tested.
 */
/* Nothing: memory comes back only when the allocator is reset or ended. */
#define EL_ALLOC_CONTRACT_RECLAIMS_NOTHING 0u
/* Freeing the allocation made by the latest successful request gives its
 * storage back: the same request then succeeds again, as often as it is
 * repeated. */
#define EL_ALLOC_CONTRACT_RECLAIMS_LATEST 1u
/* Freeing any allocation, in any order, gives its storage back: after
 * everything is freed, the same requests succeed again. Implies LATEST. */
#define EL_ALLOC_CONTRACT_RECLAIMS_ANY 2u

/*
 * How to set up the allocator under test.
 *
 * The suite uses one allocator at a time and frees everything it allocated
 * before asking for the next. Before each request it calls `reset`, if
 * there is one. `ctx` is passed back to every function unchanged.
 *
 * A fixture that cannot set an allocator up should end the process:
 * nothing was verified, and the suite cannot tell.
 */
typedef struct el_alloc_contract_fixture {
    /* Shown in failure reports. */
    const char *name;
    void *ctx;

    /*
     * Optional. Release or reset what the previous request created. Leave
     * null if the allocator needs nothing: `allocator` alone then gives
     * the suite a fresh one each time.
     */
    void (*reset)(void *ctx);

    /*
     * Required. A fresh allocator that can hold the working set above,
     * including whatever padding or overhead the allocator adds to it.
     */
    el_allocator (*allocator)(void *ctx);

    /*
     * Optional capability: bounded. Leave null if the allocator cannot be
     * given a bound; the tests that need one are reported as skipped.
     *
     * A fresh allocator that never holds more than `limit` bytes of live
     * requests (requested sizes; padding and overhead may make it refuse
     * sooner), and that can satisfy a first request of 64 bytes at any
     * alignment up to EL_ALLOC_CONTRACT_ALIGN_MAX. `limit` is
     * EL_ALLOC_CONTRACT_LIMIT.
     */
    el_allocator (*allocator_bounded)(void *ctx, size_t limit);

    /*
     * What the bounded allocator's free gives back: one of
     * EL_ALLOC_CONTRACT_RECLAIMS_*. Ignored without allocator_bounded.
     */
    uint32_t reclaims;
} el_alloc_contract_fixture;

/* ---- Checking allocator ------------------------------------------------
 *
 * Wraps another allocator and checks every call that reaches it (see the
 * end of this section) against the contract that no allocator can check
 * cheaply on its own: each free or resize names a live allocation with
 * exactly its current layout. A
 * mismatch, a double free, or a pointer it never handed out is reported on
 * standard error and ends the process with abort(), so a test that frees
 * the length of a buffer instead of its capacity fails where it happens.
 * Leaks are counted, not reported: compare el_alloc_checker_live() with 0
 * at the end of a test.
 *
 * It records each live allocation in an array the caller provides. Running
 * out of records ends the process too: the test needs a larger array. A
 * resize that moves happens inside the inner allocator's el_realloc() and
 * makes no record. But if the inner allocator refuses a resize outright,
 * el_realloc() on the checker then allocates through it, which needs one
 * free record: with none, the process ends instead of el_realloc()
 * returning NULL. Leave one spare.
 *
 * A resize (el_realloc()) through it is checked as a free is: the pointer
 * and layout must name a live allocation. It is passed to the inner
 * allocator with el_realloc(), and on success the record is the new pointer
 * and layout, so a later free with the old pointer or size is caught.
 *
 * It checks the calls that reach its operations, and only those.
 * el_realloc() answers some calls itself, before any operation: one to the
 * size the layout already has returns the pointer, and one from a layout
 * of size 0 allocates. Such a call with a wrong pointer or layout, or a
 * foreign pointer, is not seen, and is caught only when the same pointer
 * later reaches a free or a resize.
 */

typedef struct el_alloc_check_record {
    void *ptr;
    el_layout layout;
} el_alloc_check_record;

typedef struct el_alloc_checker {
    el_allocator inner;
    el_alloc_check_record *records;
    size_t cap;
    size_t live;
} el_alloc_checker;

/* Check every request made through the handle to `checker`, and pass it on to `inner`. */
void el_alloc_checker_init(el_alloc_checker *checker, el_allocator inner,
                           el_alloc_check_record *records, size_t cap);

el_allocator el_alloc_checker_as_allocator(el_alloc_checker *checker);

/* Allocations made through the checker and not yet freed. */
size_t el_alloc_checker_live(const el_alloc_checker *checker);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_ALLOC_CONTRACT_H */
