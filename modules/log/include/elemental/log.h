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
 * whatever an implementation of the one operation below does with it.
 * Several loggers and sinks coexist; there is no default logger and no
 * global state.
 *
 * This header is the contract: not variadic, usable from any language. The
 * printf-style call site and the compile-time cut-off are a C convenience
 * on top of it, in elemental/log_printf.h.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * A log call cannot fail. el_log_write() returns nothing: a call site that
 * had to handle a status would not be written. A sink's operation can fail,
 * and returns a status; when a logger's sink does not take a record, the
 * record is lost, and the sink counts it. That count lives with the sink
 * object, as every detail beyond a status does.
 *
 * Time: a logger reads its clock once for each record it makes, in
 * el_log_write(), and nowhere else. A sink never reads a clock: the record
 * carries the reading and the clock's frequency
 * (docs/adr/0020-time-representation.md).
 *
 * Ownership and lifetime:
 *
 *   - The application owns the sink objects, the clock, the logger, and
 *     every buffer. A logger borrows its sink, its clock and its tag for as
 *     long as it is used. Nothing here has anything to release.
 *   - A record is borrowed for the call. A sink copies the message bytes it
 *     keeps. The tag is the one field it may keep by pointer: a logger's
 *     tag must stay valid for as long as any sink keeps records made from
 *     it. In practice it is a string literal.
 *   - el_log_sink is a handle: two pointers, passed by value, owning
 *     nothing.
 *
 * Concurrency and execution context: el_log_write() keeps no state and takes
 * a const logger, so one logger may be used from several contexts exactly
 * when its sink and its clock may. Sinks follow the default: one object,
 * one context at a time. A log call blocks only if the sink does. Use from
 * an interrupt handler needs a sink and a clock that allow it. A sink's
 * operation must not log through a logger that leads back to the same sink.
 *
 * Contract violations, checked by assertions when the library is built with
 * them: a null logger, record, or tag; a handle that was never made; a null
 * message with a nonzero length; a record's level of EL_LOG_LEVEL_OFF or
 * above; a minimum level above EL_LOG_LEVEL_OFF.
 */
#ifndef ELEMENTAL_LOG_H
#define ELEMENTAL_LOG_H

#include <stddef.h>
#include <stdint.h>

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
 * To implement a sink, define the operation below over your own state and
 * hand out a handle that pairs the two. el_log_sink_write() validates the
 * record before your operation runs, so an implementation may rely on the
 * "called with" guarantees. Operations are called through C and must not
 * unwind.
 */
typedef struct el_log_sink_ops {
    /*
     * Take one record.
     *
     * Called with: record not null, and its fields as described above;
     *       message not null unless len is 0.
     * Must: return EL_OK having taken the whole record; or return another
     *       status, and then the record is lost. Copy what it keeps of
     *       `message`, which is not valid after the call. `tag` stays valid
     *       for as long as the logger it came from is used.
     */
    el_status (*write)(void *ctx, const el_log_record *record);
} el_log_sink_ops;

/*
 * `ops` must outlive every handle that uses it; make it static const.
 * `ctx` is passed back to the operation unchanged and may be null if the
 * implementation has no state.
 */
typedef struct el_log_sink {
    void *ctx;
    const el_log_sink_ops *ops;
} el_log_sink;

/*
 * Give `record` to `sink`. For code that drives a sink itself: a sink that
 * passes records on to others, or one that hands on records it kept.
 *
 *   EL_OK     The sink took the whole record.
 *   other     The sink's own status. The record is lost; a sink that writes
 *             text may already have passed part of a line on.
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
 * Log the `len` bytes at `message`, at `level` (below EL_LOG_LEVEL_OFF).
 *
 * If `level` is below the logger's minimum, nothing happens: the clock is
 * not read and the sink is not called. Otherwise the clock is read once,
 * and the sink is given the record. A record the sink does not take is
 * lost, and the sink counts it.
 *
 * `message` may be null only if `len` is 0; an empty message is a record.
 * It is borrowed for the call.
 */
void el_log_write(const el_logger *logger, el_log_level level, const char *message, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_LOG_H */
