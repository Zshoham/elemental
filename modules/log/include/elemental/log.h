/*
 * elemental/log.h - log records, the sink interface, and the logger.
 *
 * Logging is for applications, and for backends kept outside this
 * repository. Elemental's own modules never log.
 *
 * A logger says whose records these are and which of them matter: a tag, a
 * minimum level, the clock their time comes from, and the sink they go to.
 * A sink decides what happens to a record: it becomes a line on a stream
 * (elemental/log_writer.h), is kept in memory (elemental/log_mem.h), or
 * whatever an implementation of the two operations below does with it.
 * Several loggers and sinks coexist; there is no default logger and no
 * global state.
 *
 * A record's message is written into memory the sink lends, not into the
 * caller's: a sink begins a record by lending an el_buf, the message is
 * appended to it (formatted there, by elemental/log_printf.h), and the sink
 * ends the record. So a log call needs no buffer of its own, and how long
 * a message may be is the sink's buffer's to say, fixed or grown
 * (docs/adr/0022-log-sink-lends-the-buffer.md).
 *
 * A message the application's own formatter writes is composed there too:
 * el_log_compose() calls a function of the caller's with the lent buffer,
 * between the sink's begin and end, so the message needs no scratch buffer
 * and the library ends every record it begins, on the sink that began it.
 * Nothing is composed for a record that is not made or that the sink
 * refuses at begin; a sink that refuses the record only at end does so
 * after the function has run.
 *
 * This header is the contract: not variadic, usable from any language. The
 * printf-style call site and the compile-time cut-off are a C convenience
 * on top of it, in elemental/log_printf.h.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * A log call cannot fail. el_log_write() returns nothing: a call site that
 * had to handle a status would not be written. A sink's operations can
 * fail, and return a status; when a logger's sink does not take a record,
 * the record is lost, and the sink counts it. That count lives with the
 * sink object, as every detail beyond a status does.
 *
 * Time: a logger reads its clock once for each record it makes, before the
 * sink is given it, and nowhere else. A sink never reads a clock: the
 * record carries the reading and the clock's frequency
 * (docs/adr/0020-time-representation.md).
 *
 * Ownership and lifetime:
 *
 *   - The application owns the sink objects, the clock, the logger, and
 *     every buffer. A logger borrows its sink, its clock and its tag for as
 *     long as it is used. Nothing here has anything to release.
 *   - A record is borrowed for the call. Its message is in the buffer the
 *     sink lent, and valid until the sink's end operation returns: a sink
 *     that keeps it either keeps it there or copies it. The tag is the one
 *     field it may keep by pointer: a logger's tag must stay valid for as
 *     long as any sink keeps records made from it. In practice it is a
 *     string literal.
 *   - el_log_sink is a handle: two pointers, passed by value, owning
 *     nothing.
 *
 * Concurrency and execution context: el_log_write() keeps no state and takes
 * a const logger, so one logger may be used from several contexts exactly
 * when its sink and its clock may. Sinks follow the default: one object,
 * one context at a time; a record's begin and end are made from the same
 * context, with nothing else given to that sink between them. A log call
 * blocks only if the sink does, or, for el_log_compose(), the function it
 * was given does. Use from an interrupt handler needs a sink and a clock
 * that allow it, a sink's buffer that does (a grown one allocates), and,
 * for el_log_compose(), a compose function that does: it runs on the
 * caller's stack, in the caller's context, and whatever it calls may block
 * or allocate there. A sink's operations, and a function given to
 * el_log_compose(), must not log through a logger that leads back to the
 * same sink.
 *
 * Contract violations, checked by assertions when the library is built with
 * them: a null logger, record, or tag; a handle that was never made, or one
 * with an operation missing; a null message with a nonzero length; a
 * record's level of EL_LOG_LEVEL_OFF or above; a minimum level above
 * EL_LOG_LEVEL_OFF; a sink that begins a record without lending a buffer; a
 * null compose function; a compose function that returns with the buffer
 * shorter than it was when it was called; beginning a record on a sink that
 * has one open, by logging into it from inside a compose function or from
 * a callback the sink calls while it ends a record, such as a log writer
 * sink's writer (the writer, memory and deferred sinks check it). A
 * compose function that truncates below that length and appends again,
 * ending no shorter, breaks the same precondition, but only the length it
 * returns with is checked: the sink's own bytes it overwrote go unnoticed.
 */
#ifndef ELEMENTAL_LOG_H
#define ELEMENTAL_LOG_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/io_buf.h>
#include <elemental/status.h>
#include <elemental/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Levels --------------------------------------------------------------
 *
 * How much a record matters. Ordered: a larger value matters more.
 */
typedef uint8_t el_log_level;

enum {
    EL_LOG_LEVEL_DEBUG = 0,
    EL_LOG_LEVEL_INFO = 1,
    EL_LOG_LEVEL_WARN = 2,
    EL_LOG_LEVEL_ERROR = 3,
    /* Not a record's level. As a logger's minimum: log nothing. */
    EL_LOG_LEVEL_OFF = 4
};

/*
 * A short, stable, lower-case name for a level: "debug", "info", "warn",
 * "error", "off". Any other value yields "unknown". Never returns NULL; the
 * string has static storage duration.
 */
const char *el_log_level_name(el_log_level level);

/* ---- Records and sinks --------------------------------------------------- */

/* One record, as a sink receives it. */
typedef struct el_log_record {
    el_ticks timestamp;        /* the logger's clock when the record was made */
    uint32_t ticks_per_second; /* that clock's frequency, at least 1 */
    el_log_level level;        /* below EL_LOG_LEVEL_OFF */
    const char *tag;           /* the logger's tag: NUL-terminated, never null */
    const char *message;       /* `len` bytes, not terminated */
    size_t len;
} el_log_record;

/*
 * To implement a sink, define the two operations below over your own state
 * and hand out a handle that pairs them with it. A record reaches a sink in
 * three steps:
 *
 *   1. begin: the sink is given the record without its message, and lends
 *      the el_buf the message goes into. The buffer may already hold bytes
 *      of the sink's own, such as the start of a line.
 *   2. The caller appends the message to that buffer, and does nothing
 *      else with it: el_buf_append(), el_fmt_printf(), or el_buf_reserve()
 *      and el_buf_commit(), and el_buf_truncate() no lower than the length
 *      the buffer had when begin returned. A message the buffer cannot take
 *      whole is cut, by printf's item rule or at a byte.
 *   3. end: the sink is given the whole record, its message being the bytes
 *      appended, and says whether it took it.
 *
 * el_log_sink_write() and the logger make these calls, and validate the
 * record before `begin`, so an implementation may rely on the "called with"
 * guarantees. Operations are called through C and must not unwind.
 */
typedef struct el_log_sink_ops {
    /*
     * Start a record.
     *
     * Called with: record not null, its fields as described above except
     *       that message is null and len is 0; message not null.
     * Must: return EL_OK and set *message to the buffer the caller appends
     *       the message to, after which `end` is called; or return another
     *       status, and then the record is lost, `end` is not called, and
     *       *message is not read.
     */
    el_status (*begin)(void *ctx, const el_log_record *record, el_buf **message);

    /*
     * Finish the record the last `begin` started.
     *
     * Called with: record not null, with the fields `begin` was given, and
     *       its message: the `len` bytes appended to the lent buffer since
     *       `begin` returned, in that buffer (null if len is 0). `fit` is
     *       EL_OK if the message is whole, EL_ERR_NO_SPACE if the buffer
     *       could not take all of it and it is cut.
     * Must: return EL_OK having taken the whole record, which a cut message
     *       is not; or return another status, and then the record is lost.
     *       The message is not valid after the call, unless the sink keeps
     *       it in the buffer it lent. `tag` stays valid for as long as the
     *       logger it came from is used.
     */
    el_status (*end)(void *ctx, const el_log_record *record, el_status fit);
} el_log_sink_ops;

/*
 * `ops` must outlive every handle that uses it; make it static const.
 * `ctx` is passed back to the operations unchanged and may be null if the
 * implementation has no state.
 */
typedef struct el_log_sink {
    void *ctx;
    const el_log_sink_ops *ops;
} el_log_sink;

/*
 * Give `record` to `sink`: begin it, append its message to the buffer the
 * sink lends, and end it. A message the buffer cannot take whole is cut at
 * a byte, and the sink is told. For code that drives a sink itself: a sink
 * that passes records on to others, or one that hands on records it kept.
 * `record->message` must not be in the buffer the sink lends.
 *
 *   EL_OK     The sink took the whole record.
 *   other     The sink's own status, from begin or end. The record is lost;
 *             a sink that writes text may already have passed part of a
 *             line on.
 */
EL_NODISCARD el_status el_log_sink_write(el_log_sink sink, const el_log_record *record);

/* ---- The logger ----------------------------------------------------------
 *
 * The struct layout is public so that callers can allocate it. The fields
 * are not part of the API: use the functions. A logger may be copied or
 * moved freely; it holds only what it borrows.
 */
typedef struct el_logger {
    el_log_sink sink;
    el_clock clock;
    const char *tag;
    uint32_t ticks_per_second;
    el_log_level level_min;
} el_logger;

/*
 * Set a logger up.
 *
 *   sink       where its records go.
 *   clock      where their time comes from. Its frequency is read here,
 *              once.
 *   tag        names the part of the program that logs through this logger;
 *              "" for none. Borrowed: see "Ownership and lifetime".
 *   level_min  records below it are not made. EL_LOG_LEVEL_OFF: none are.
 */
void el_logger_init(el_logger *logger, el_log_sink sink, el_clock clock, const char *tag,
                    el_log_level level_min);

/* Change the least level that is let through. */
void el_logger_set_level(el_logger *logger, el_log_level level_min);

/*
 * The logger that makes no records, for a component that must be handed a
 * logger in a program that wants none of its records. Its minimum is
 * EL_LOG_LEVEL_OFF: every call returns at once, reads no clock and gives
 * nothing to a sink. Constant, valid for the whole program, and usable
 * from any context.
 */
const el_logger *el_logger_off(void);

/*
 * Log the `len` bytes at `message`, at `level` (below EL_LOG_LEVEL_OFF).
 *
 * If `level` is below the logger's minimum, nothing happens: the clock is
 * not read and the sink is not called. Otherwise the clock is read once,
 * and the record is given to the sink as el_log_sink_write() gives it: the
 * message is copied into the sink's buffer, and cut if it does not fit. A
 * record the sink does not take whole is lost, and the sink counts it.
 *
 * `message` may be null only if `len` is 0; an empty message is a record.
 * It is borrowed for the call.
 */
void el_log_write(const el_logger *logger, el_log_level level, const char *message, size_t len);

/*
 * Writes a record's message into `message`, the buffer the logger's sink
 * lends, with el_buf functions only: el_buf_append(), el_fmt_printf(), a
 * formatter of your own, el_buf_reserve() with el_buf_commit(), and
 * el_buf_truncate() back to no less than the length the buffer had when
 * it was called. Returns EL_OK if the message is whole; any other status,
 * such as the first failure of what it called, means it is cut.
 *
 * It runs while the sink holds the record open: it must not log through a
 * logger whose records reach the same sink, as a sink's own operations
 * must not. It runs in the context of the el_log_compose() call, on its
 * stack, so it and whatever it calls must be fit for that context: from
 * an interrupt handler it must not block or allocate. Called through C:
 * it must not unwind.
 */
typedef el_status (*el_log_compose_fn)(void *ctx, el_buf *message);

/*
 * Log the message `compose` writes, at `level` (below EL_LOG_LEVEL_OFF).
 *
 * If `level` is below the logger's minimum, nothing happens: the clock is
 * not read and `compose` is not called. Otherwise the record is begun; if
 * the sink refuses it, it is lost and counted, and `compose` is not
 * called. Otherwise compose(ctx, message) writes the message into the
 * buffer the sink lent, and the record is ended on the sink that began it,
 * even if `compose` changed the logger meanwhile. A status other than
 * EL_OK reaches the sink as EL_ERR_NO_SPACE, a cut message, whatever the
 * failure was. A record the sink does not take at end is lost, and the
 * sink counts it; `compose` has run by then. Like every log call, it
 * cannot fail.
 *
 * `ctx` is passed to `compose` unchanged, and may be null.
 */
void el_log_compose(const el_logger *logger, el_log_level level, el_log_compose_fn compose,
                    void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_LOG_H */
