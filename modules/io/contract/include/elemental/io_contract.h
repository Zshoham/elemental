/*
 * elemental/io_contract.h - the fixture for the io contract suite.
 *
 * Every el_reader and el_writer implementation is expected to pass the io
 * contract suite (elemental/io_contract.hpp). The suite checks what the
 * interface promises (elemental/io.h), not what a particular platform does;
 * platform behavior belongs in the backend's own tests.
 *
 * A fixture tells the suite how to set up the implementation under test. It
 * is plain C, so a backend written in C is described in C; only the line
 * that hands the fixture to the suite is C++.
 *
 * See modules/io/tests/fixture_mem.c for the smallest complete fixture.
 */
#ifndef ELEMENTAL_IO_CONTRACT_H
#define ELEMENTAL_IO_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/io.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The most bytes the suite asks a fixture to carry in one stream. */
#define EL_IO_CONTRACT_SIZE_MAX 256u

/*
 * How to set up the implementation under test.
 *
 * The suite uses one stream at a time. Before asking for a new one it calls
 * `reset`, which must release whatever the previous request created. `ctx`
 * is passed back to every function unchanged.
 *
 * Sizes passed in never exceed EL_IO_CONTRACT_SIZE_MAX.
 *
 * Streams may return EL_ERR_WOULD_BLOCK where the contract allows it; the
 * suite retries a bounded number of times, so a stream that can make
 * progress must do so within 64 consecutive attempts.
 *
 * A fixture that cannot set a stream up (the platform refused) should end
 * the process: nothing was verified, and the suite cannot tell.
 */
typedef struct el_io_contract_fixture {
    /* Shown in failure reports. */
    const char *name;
    void *ctx;

    /* Required. Release the stream created by the previous request, if any. */
    void (*reset)(void *ctx);

    /*
     * Required. A reader that yields exactly content[0..len) and then ends.
     * `content` stays valid until the next `reset`.
     */
    el_reader (*reader_with_content)(void *ctx, const uint8_t *content, size_t len);

    /* Required. A writer that accepts at least `room` bytes. */
    el_writer (*writer_with_room)(void *ctx, size_t room);

    /*
     * Required. Copy out, in order, the bytes the current writer's sink has
     * received and not yet handed over, up to `cap`. Returns the count.
     */
    size_t (*writer_take)(void *ctx, uint8_t *out, size_t cap);

    /*
     * Optional capabilities. Leave null if the implementation cannot be put
     * in that state; the suite reports the corresponding test as skipped.
     */

    /* A reader with nothing to give right now that has not ended. */
    el_reader (*reader_stalled)(void *ctx);

    /* A writer that can accept nothing right now but has not ended. */
    el_writer (*writer_stalled)(void *ctx);

    /* A writer that accepts exactly `room` bytes and then has ended. */
    el_writer (*writer_ending_after)(void *ctx, size_t room);
} el_io_contract_fixture;

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_IO_CONTRACT_H */
