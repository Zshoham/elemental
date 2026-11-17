/*
 * elemental/log_mem.h - a sink that keeps records in caller-provided memory.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Records are kept as records, not as text: a test reads back a level, a
 * time and a message without parsing a line. For tests of code that logs,
 * and for looking at what was logged after the fact. (Lines of text in
 * memory are the writer sink over an el_buf: elemental/log_writer.h.)
 *
 * Records are kept until the sink is cleared. A message is written
 * straight into the storage, where it is kept (el_log_printf() formats it
 * there), so the storage that is left is the only limit on its length. A
 * record that does not fit is not kept, in whole, and is counted as lost;
 * nothing already kept is overwritten.
 *
 * Storage and ownership: the caller provides the object and the storage,
 * of any alignment, and keeps both alive while the sink, any handle made
 * from it, or any record read from it is in use. A kept record's tag is the
 * pointer the logger had: it must still be valid when the record is read
 * (elemental/log.h, "Ownership and lifetime"). Nothing to destroy. The
 * struct layout is public so that callers can allocate it; use the
 * functions.
 *
 * Concurrency: no internal synchronization: one object, one context at a
 * time. Nothing here blocks.
 */
#ifndef ELEMENTAL_LOG_MEM_H
#define ELEMENTAL_LOG_MEM_H

#include <elemental/io_buf.h>
#include <elemental/log.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el_log_mem_sink {
    uint8_t *storage;
    size_t cap;
    size_t len;
    size_t count;
    el_buf message; /* lent for the record begun: the storage after its entry */
    uint32_t lost;
} el_log_mem_sink;

/*
 * The bytes of storage one record with a message of `message_len` bytes
 * takes, for sizing the storage: n records need the sum of theirs.
 * `message_len` must leave room for the record's own fields below SIZE_MAX.
 */
size_t el_log_mem_sink_record_size(size_t message_len);

/*
 * Keep records in the `cap` bytes at `storage`. `storage` may be null only
 * if `cap` is 0, which makes a sink that loses every record.
 */
void el_log_mem_sink_init(el_log_mem_sink *sink, void *storage, size_t cap);

/*
 * A sink handle for `sink`. Its begin lends the storage that is left after
 * the record's own fields, and returns EL_ERR_NO_SPACE if not even those
 * fit. Its end returns:
 *
 *   EL_OK             The record is kept.
 *   EL_ERR_NO_SPACE   Its message did not fit in what is left of the
 *                     storage. It is not kept.
 *
 * Either EL_ERR_NO_SPACE counts the record as lost.
 */
el_log_sink el_log_mem_sink_as_sink(el_log_mem_sink *sink);

/* Records kept. */
size_t el_log_mem_sink_count(const el_log_mem_sink *sink);

/*
 * The record at `index`, which must be below the count; 0 is the oldest.
 * `record` is required. Its message points into the sink's storage and is
 * valid until the sink is cleared or set up again.
 */
void el_log_mem_sink_get(const el_log_mem_sink *sink, size_t index, el_log_record *record);

/* Records that did not fit, since the sink was set up or last cleared. */
uint32_t el_log_mem_sink_lost(const el_log_mem_sink *sink);

/* Forget every record, and the lost count. The storage is kept. */
void el_log_mem_sink_clear(el_log_mem_sink *sink);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_LOG_MEM_H */
