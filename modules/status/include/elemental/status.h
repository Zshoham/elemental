/*
 * elemental/status.h - status codes for recoverable failures, and the
 * spellings for handling one.
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
 *   - EL_OK is zero. Every code Elemental defines is a positive EL_ERR_*
 *     value. Negative values belong to the application (below).
 *   - Output parameters are always written, on every return. What they hold
 *     for each status is part of the function's documentation.
 *   - The status must be used. The function is declared EL_NODISCARD, so a
 *     call that drops its status is reported by the compiler.
 *
 * The codes are a shared vocabulary, kept small on purpose. A code is added
 * when a caller would handle it differently from every existing code, and a
 * module in this repository returns it. Values are never reused or
 * renumbered. Detail beyond the code (an errno, a device register) stays
 * with the object that failed and is read from that object.
 *
 * Handling a status. A caller does one of four things with it, and each has
 * a spelling:
 *
 *   act on it              if (status == EL_ERR_WOULD_BLOCK) { ... }
 *   pass it on             EL_TRY(el_write_all(out, data, len, NULL));
 *   pass it on, after      EL_TRY_GOTO(el_write_all(out, data, len, NULL),
 *     releasing something              status, cleanup);
 *   discard it             el_status_ignore(el_buffered_writer_flush(&w));
 *
 * and a function that finds a failure itself, in data it was given, reports
 * it with EL_ENSURE(condition, status). All of them are at the end of this
 * header.
 *
 * Application statuses. Elemental defines no negative code and never
 * returns one of its own. A negative status reaches a caller only from the
 * application's code: a function of its own, or an operation it implemented
 * behind an Elemental interface, whose status Elemental's functions return
 * unchanged. So an application that writes its own el_status functions
 * numbers its failures below zero and they cannot collide with a code added
 * here later. Where the platform reports failures as negative errno values
 * (Zephyr, the Linux kernel), such a value is an application status as it
 * stands; see EL_TRY_NEG.
 *
 * Thread safety: everything here is a constant, a pure function, or a macro
 * that touches only what its arguments name.
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
 * defined code yields "unknown", an application's negative status included:
 * the application names its own.
 *
 * Never returns NULL. The string has static storage duration; do not free
 * or modify it.
 */
const char *el_status_name(el_status status);

/* ---- Statuses must be used ------------------------------------------------
 *
 * EL_NODISCARD goes in front of the declaration of every function whose
 * result reports a failure: each function that returns el_status, and an
 * allocation that returns NULL. Put it on your own:
 *
 *     EL_NODISCARD el_status record_write(el_writer out, const char *text);
 *
 * A call that drops the result is then a warning from GCC and Clang in
 * every language mode, and from any C23 or C++17 compiler. Elsewhere the
 * macro expands to nothing. Results reached through an operation table are
 * not covered: the functions in front of the table are.
 */
#if defined(__cplusplus) && __cplusplus >= 201703L
#define EL_NODISCARD [[nodiscard]]
#elif !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define EL_NODISCARD [[nodiscard]]
#elif defined(__GNUC__) || defined(__clang__)
#define EL_NODISCARD __attribute__((warn_unused_result))
#else
#define EL_NODISCARD
#endif

/*
 * Discard a status on purpose: for the call whose failure changes nothing
 * the caller does next, such as a last flush on a path that is already
 * reporting an earlier failure. It says so where a reader will see it, and
 * it is the one spelling every compiler accepts: GCC keeps warning through
 * a (void) cast before C23.
 */
static inline void el_status_ignore(el_status status)
{
    (void)status;
}

/* ---- Passing a status on --------------------------------------------------
 *
 * These are macros because each leaves the function it is written in, which
 * no function can do for its caller. Each is one statement, evaluates its
 * expression exactly once, and needs nothing but this header.
 */

/*
 * If `expr` is not EL_OK, return it from the enclosing function. For a
 * function that returns el_status and holds nothing to release:
 *
 *     static el_status record_write(el_writer out, const char *text, size_t len)
 *     {
 *         const uint8_t header = (uint8_t)len;
 *         EL_TRY(el_write_all(out, &header, 1, NULL));
 *         return el_write_all(out, text, len, NULL);
 *     }
 *
 * Every status other than EL_OK returns, EL_ERR_WOULD_BLOCK and
 * EL_ERR_END_OF_STREAM included. Where one of them is a condition the
 * function handles, compare first and pass on the rest.
 */
#define EL_TRY(expr)                             \
    do {                                         \
        const el_status el_try_status_ = (expr); \
        if (el_try_status_ != EL_OK) {           \
            return el_try_status_;               \
        }                                        \
    } while (0)

/*
 * Store `expr` in `status`, and jump to `label` if it is not EL_OK. For a
 * function that must release something before it returns:
 *
 *     el_status status;
 *     EL_TRY_GOTO(el_write_all(out, head, head_len, NULL), status, done);
 *     EL_TRY_GOTO(el_write_all(out, body, body_len, NULL), status, done);
 * done:
 *     el_free(allocator, body, layout);
 *     return status;
 *
 * `status` is assigned either way, so falling into the label after the last
 * step leaves EL_OK in it.
 */
#define EL_TRY_GOTO(expr, status, label) \
    do {                                 \
        (status) = (expr);               \
        if ((status) != EL_OK) {         \
            goto label;                  \
        }                                \
    } while (0)

/*
 * For a platform call that returns a negative errno value on failure and
 * zero or more on success (Zephyr, the Linux kernel): if `expr` is negative,
 * return it from the enclosing function as an application status.
 *
 *     const int got = uart_fifo_read(dev, buf, cap);
 *     EL_TRY_NEG(got);
 *
 * A call that returns only zero or a negative errno can be given to EL_TRY
 * as it is. Not for POSIX calls that return -1 and set errno: the -1 is not
 * the failure. `expr` may have any signed integer type; a negative value
 * must fit el_status.
 */
#define EL_TRY_NEG(expr)                      \
    do {                                      \
        const int64_t el_try_value_ = (expr); \
        if (el_try_value_ < 0) {              \
            return (el_status)el_try_value_;  \
        }                                     \
    } while (0)

/*
 * Unless `condition` holds, return `status` from the enclosing function.
 * The recoverable counterpart of EL_ASSERT(): EL_ASSERT states what only a
 * bug can make false, EL_ENSURE what input can.
 *
 *     EL_ENSURE(len <= UINT8_MAX, EL_ERR_NO_SPACE);
 *
 * `status` is evaluated only when the condition fails.
 */
#define EL_ENSURE(condition, status) \
    do {                             \
        if (!(condition)) {          \
            return (status);         \
        }                            \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_STATUS_H */
