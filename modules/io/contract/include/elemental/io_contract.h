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
 * See modules/io/tests/fixture_buf.c for the smallest complete fixture.
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
 * What a reader does once it has given its content: the fixture's
 * reader_then. 0 is EL_IO_CONTRACT_THEN_ENDS, so a fixture written before
 * the field existed keeps its meaning.
 */
/* EL_ERR_END_OF_STREAM, for good: memory, a pipe whose write end is
 * closed, a closed ring. */
#define EL_IO_CONTRACT_THEN_ENDS 0u
/* EL_ERR_WOULD_BLOCK: not ended, and more may come. A UART, an open
 * socket, a ring whose producer has not closed it. */
#define EL_IO_CONTRACT_THEN_STALLS 1u

/*
 * How to set up the implementation under test.
 *
 * A fixture has a reader half, a writer half, or both; at least one. A half
 * that is there has its required functions, and the suite reports every
 * test of an absent half as skipped ("no reader", "no writer"). A
 * write-only implementation leaves the reader half out; it does not borrow
 * another implementation's reader to fill it. An optional function of an
 * absent half, or a `reader_then` that is neither constant, fails the
 * test's set-up: the tests it was meant for would be skipped unnoticed.
 *
 * The suite uses one stream at a time. Before asking for a new one it calls
 * `reset`, if the fixture has one, which must release whatever the previous
 * request created. `ctx` is passed back to every function unchanged.
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

    /*
     * Optional. Release the stream created by the previous request, if any.
     * Leave null if there is nothing to release: the suite does not call it.
     */
    void (*reset)(void *ctx);

    /*
     * The reader half is reader_with_content, with the optional
     * reader_stalled and reader_then. The writer half is writer_with_room
     * and writer_take, with the optional writer_stalled,
     * writer_ending_after and writer_full_after. The members keep the order
     * they had before either half could be absent, so a fixture initialized
     * by position, or with C++20 designated initializers in that order,
     * still compiles and means the same; reader_then, added later, is last.
     */

    /*
     * Required for the reader half. A reader that yields exactly
     * content[0..len) and then does what `reader_then` says. `content` is
     * the suite's, constant and of static storage duration, so it stays
     * valid for as long as the reader borrows it, including after the test
     * that asked for it.
     *
     * Every byte of `content` can be read once this returns: the suite
     * allows a reader 64 stalls in a row, not the time a wire takes. A
     * fixture over a device waits for its bytes to arrive before it
     * returns.
     */
    el_reader (*reader_with_content)(void *ctx, const uint8_t *content, size_t len);

    /* Required for the writer half. A writer that accepts at least `room` bytes. */
    el_writer (*writer_with_room)(void *ctx, size_t room);

    /*
     * Required with writer_with_room. Copy out, in order, the bytes the
     * current writer's sink has received and not yet handed over, up to
     * `cap`. Returns the count.
     */
    size_t (*writer_take)(void *ctx, uint8_t *out, size_t cap);

    /*
     * Optional capabilities. Leave null if the implementation cannot be put
     * in that state; the suite reports the corresponding test as skipped.
     */

    /* Reader half. A reader with nothing to give right now that has not ended. */
    el_reader (*reader_stalled)(void *ctx);

    /* Writer half. A writer that can accept nothing right now but has not ended. */
    el_writer (*writer_stalled)(void *ctx);

    /* Writer half. A writer that accepts exactly `room` bytes and then has ended. */
    el_writer (*writer_ending_after)(void *ctx, size_t room);

    /*
     * Writer half. A writer that accepts exactly `room` bytes and then
     * reports EL_ERR_NO_SPACE, which is not permanent: a writer that stores
     * what it takes, such as a full memory buffer. After the members that
     * came before it, so that a fixture written before it existed, with
     * designated initializers, leaves it null.
     */
    el_writer (*writer_full_after)(void *ctx, size_t room);

    /*
     * Reader half. What the reader from reader_with_content does after its
     * content: EL_IO_CONTRACT_THEN_ENDS (0, the default) or
     * EL_IO_CONTRACT_THEN_STALLS. A reader that ends is checked to end for
     * good; one that stalls is checked to report EL_ERR_WOULD_BLOCK and
     * never an end. Used only with reader_with_content. Last, so that a
     * fixture written before it existed, by position or with designated
     * initializers, leaves it 0.
     */
    uint32_t reader_then;
} el_io_contract_fixture;

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_IO_CONTRACT_H */
