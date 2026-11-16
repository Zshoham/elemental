/*
 * elemental/log_writer.h - a sink that writes each record as a line of text.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Each record becomes one line, built in storage the caller provides and
 * passed to the writer with one el_write_all():
 *
 *     [    12.034500] W net: retrying in 2 s
 *     [     0.250300] I started
 *
 *   - "[" the whole seconds of the record's timestamp, right-aligned in six
 *     columns (wider if it needs more), ".", six digits of microseconds,
 *     rounded down, "] ";
 *   - the level's letter, D, I, W or E, and a space;
 *   - the tag, a colon and a space, unless the tag is empty;
 *   - the message, as it is: a newline in it makes two lines;
 *   - a newline.
 *
 * The format is fixed. A program that wants another writes a sink of its
 * own: it receives the same record.
 *
 * A line longer than the storage is cut to fit and still ends in a newline.
 *
 * Meant for a writer that does not stall: a blocking descriptor, an el_buf.
 * When the writer stalls or fails part-way, the stream is left mid-line and
 * the rest of that line is not sent later. The sink holds nothing between
 * records, so there is nothing to flush: a buffered writer under it is
 * flushed by whoever created that writer.
 *
 * Storage and ownership: the caller provides the object and the line
 * storage and keeps both alive while the sink or any handle made from it is
 * in use. The sink borrows the writer's object for the same time. Nothing
 * to destroy. The struct layout is public so that callers can allocate it;
 * use the functions.
 *
 * Concurrency: no internal synchronization: one object, one context at a
 * time. It blocks only if the writer does.
 */
#ifndef ELEMENTAL_LOG_WRITER_H
#define ELEMENTAL_LOG_WRITER_H

#include <elemental/io.h>
#include <elemental/io_buf.h>
#include <elemental/log.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el_log_writer_sink {
    el_writer out;
    el_buf line;
    uint32_t lost;
} el_log_writer_sink;

/*
 * Write lines to `out`, building each in the `line_cap` bytes at
 * `line_storage`. `line_storage` may be null only if `line_cap` is 0; a
 * sink with no storage loses every record. Eighty bytes hold the prefix, a
 * short tag, and some fifty bytes of message.
 */
void el_log_writer_sink_init(el_log_writer_sink *sink, el_writer out, void *line_storage,
                             size_t line_cap);

/*
 * A sink handle for `sink`. Its write returns:
 *
 *   EL_OK             The whole line went out.
 *   EL_ERR_NO_SPACE   The line did not fit the storage. It went out cut,
 *                     ending in a newline.
 *   other             The writer's status from el_write_all(). The stream
 *                     may hold the first part of the line.
 *
 * Anything but EL_OK counts the record as lost.
 */
el_log_sink el_log_writer_sink_as_sink(el_log_writer_sink *sink);

/* Records that did not go out whole, since the sink was set up. */
uint32_t el_log_writer_sink_lost(const el_log_writer_sink *sink);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_LOG_WRITER_H */
