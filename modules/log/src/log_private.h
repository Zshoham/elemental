/*
 * Shared inside log; not installed. How a record is made and given to a
 * sink, for el_log_write(), el_log_sink_write() and el_log_vprintf().
 */
#ifndef ELEMENTAL_LOG_PRIVATE_H
#define ELEMENTAL_LOG_PRIVATE_H

#include <stddef.h>

#include <elemental/log.h>

/*
 * The record `logger` makes at `level`, without its message: the clock is
 * read here. The caller has checked the logger and the level's minimum.
 */
void el_log_record_start_(const el_logger *logger, el_log_level level, el_log_record *record);

/*
 * Check the handle and `record` (message null, len 0), and begin it. On
 * EL_OK, *message is the buffer the sink lent and *start its length then:
 * where the message begins.
 */
EL_NODISCARD el_status el_log_sink_begin_(el_log_sink sink, const el_log_record *record,
                                          el_buf **message, size_t *start);

/*
 * End the record begun with el_log_sink_begin_(): its message is what was
 * appended to `message` after `start`. `record` is updated to hold it.
 */
EL_NODISCARD el_status el_log_sink_end_(el_log_sink sink, el_log_record *record,
                                        const el_buf *message, size_t start, el_status fit);

#endif /* ELEMENTAL_LOG_PRIVATE_H */
