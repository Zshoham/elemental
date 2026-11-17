/*
 * elemental/log_writer.h - a sink that writes each record as a line of text.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Each record becomes one line, built in an el_buf the caller gives it and
 * passed to the writer with one el_write_all(). The sink lends that buffer
 * for the record's message (elemental/log.h): it writes the line's start,
 * the message is appended (el_log_printf() formats it there), and the sink
 * adds the newline and writes the line. So the message is copied once, or
 * not at all.
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
 * A line that does not fit in the buffer is cut and still ends in a newline:
 * fixed storage too small for it, or grown storage whose allocator refused
 * to grow it that far. The buffer is the only limit on a message's length.
 *
 * Meant for a writer that does not stall: a blocking descriptor, an el_buf.
 * When the writer stalls or fails part-way, the stream is left mid-line and
 * the rest of that line is not sent later. The sink holds nothing between
 * records, so there is nothing to flush: a buffered writer under it is
 * flushed by whoever created that writer.
 *
 * Storage and ownership: the caller provides the object and the line buffer,
 * an el_buf (elemental/io_buf.h) with fixed or grown storage, and keeps
 * both alive while the sink or any handle made from it is in use. The sink
 * borrows the buffer, by pointer, for that time, and the writer's object
 * likewise. While it does, the buffer's contents are the sink's: it empties
 * the buffer when it begins each record, and its owner must not use it for
 * anything else, the sink's writer included. A grown buffer keeps the
 * capacity of the longest line it has held; its owner frees it
 * (el_grow_buf_deinit) once the sink is no longer used. The sink itself
 * has nothing to destroy. The struct layout is public so that callers can
 * allocate it; use the functions.
 *
 * Concurrency: no internal synchronization: one object, one context at a
 * time, and the buffer goes with it. It blocks only if the writer does, or
 * a grown buffer's allocator. Over a grown buffer it may be used only where
 * that allocator may.
 *
 * Contract violations, checked by assertions: a null object or line buffer;
 * a writer handle that was never made.
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
    el_buf *line;      /* borrowed, not copied: a grown el_buf grows only at its own address */
    size_t prefix_len; /* of the record begun: the line before its message */
    el_status prefix;  /* EL_ERR_NO_SPACE if that start of the line was cut */
    uint32_t lost;
} el_log_writer_sink;

/*
 * Write lines to `out`, building each in `line`, fixed or grown, which the
 * sink borrows until it is no longer used. Fixed storage of eighty bytes
 * holds the prefix, a short tag, and some fifty bytes of message. A buffer
 * with no storage (el_buf_init(&line, NULL, 0)) makes a sink that loses
 * every record.
 */
void el_log_writer_sink_init(el_log_writer_sink *sink, el_writer out, el_buf *line);

/*
 * A sink handle for `sink`. Its begin always lends the line; its end
 * returns:
 *
 *   EL_OK             The whole line went out.
 *   EL_ERR_NO_SPACE   The line did not fit the buffer, which could not grow
 *                     enough, or the message came cut. It went out cut,
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
