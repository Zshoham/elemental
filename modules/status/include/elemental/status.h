/*
 * elemental/status.h - status codes for recoverable failures.
 *
 * A status says why an operation did not do what was asked, in terms a
 * caller can act on. It is not for programmer mistakes: passing a null
 * pointer or misusing an object violates a contract and is caught by an
 * assertion instead (elemental/assert.h).
 *
 * Conventions used by every Elemental function that can fail at run time:
 *
 *   - The function returns el_status. Results go to output parameters.
 *     A status and a value are never packed into one return value.
 *   - EL_OK is zero. Every other code is a positive EL_ERR_* value.
 *   - Output parameters are always written, on every return. What they hold
 *     for each status is part of the function's documentation.
 *
 * The codes are a shared vocabulary, kept small on purpose. A code is added
 * when a caller would handle it differently from every existing code, and a
 * module in this repository returns it. Values are never reused or
 * renumbered. Detail beyond the code (an errno, a device register) stays
 * with the object that failed and is read from that object.
 *
 * Thread safety: everything here is a constant or a pure function.
 */
#ifndef ELEMENTAL_STATUS_H
#define ELEMENTAL_STATUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A fixed-width integer rather than the enum type: the size of a C enum is
 * chosen by the compiler (and by flags such as -fshort-enums), so an enum in
 * a signature or a struct would not have one ABI.
 */
typedef int32_t el_status;

enum {
    /* The operation did what was asked. */
    EL_OK = 0,

    /*
     * Nothing could be done right now, and nothing is wrong. The same call
     * can succeed later, once the other side makes progress. Returned only
     * by operations documented as non-blocking.
     */
    EL_ERR_WOULD_BLOCK = 1,

    /*
     * The stream has ended: a reader has no more bytes to give, a writer
     * can accept no more. Permanent for that stream. This is the expected
     * way for a stream to finish, and callers commonly treat it as a
     * condition rather than a fault.
     */
    EL_ERR_END_OF_STREAM = 2,

    /*
     * The environment failed: a device, the operating system, a transport.
     * The object that reported it holds the platform-specific detail.
     */
    EL_ERR_IO = 3,

    /*
     * The storage available to the operation was not enough: a buffer the
     * caller supplied is too small, a container is full, or an allocator
     * is exhausted inside a function that allocates. The same call can
     * succeed with more storage. What the outputs hold is documented by
     * each function that returns it.
     */
    EL_ERR_NO_SPACE = 4
};

/*
 * A short, stable, lower-case name for a status, for logs and diagnostics:
 * "ok", "would_block", "end_of_stream", "io", "no_space". Any value that is not a
 * defined code yields "unknown".
 *
 * Never returns NULL. The string has static storage duration; do not free
 * or modify it.
 */
const char *el_status_name(el_status status);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_STATUS_H */
