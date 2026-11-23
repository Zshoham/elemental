/*
 * Shared inside log; not installed. How a record is made and given to a
 * sink, for el_log_write(), el_log_sink_write(), el_log_compose() and
 * el_log_vprintf().
 */
#ifndef ELEMENTAL_LOG_PRIVATE_H
#define ELEMENTAL_LOG_PRIVATE_H

#include <stdbool.h>
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

/*
 * A record a logger's call has begun and must end: the sink that began it,
 * the record, and the buffer the sink lent with where the message begins.
 * The sink is kept here, not read from the logger again at the end, so
 * the record ends on the sink that began it even if the caller's function
 * changed the logger in between.
 */
typedef struct el_log_open_record_ {
    el_log_sink sink;
    el_log_record record;
    el_buf *message;
    size_t start;
} el_log_open_record_;

/*
 * The steps a logger's call that writes its own message takes first, for
 * el_log_compose() and el_log_vprintf(): check the logger, apply its
 * minimum, make the record (the clock is read here) and begin it. True if
 * the record was begun: *open holds it, its message and start are as
 * el_log_sink_begin_() gives them, and el_log_close_() must follow. False
 * if the level is below the minimum, or the sink refused the record, which
 * is then lost and counted: nothing is to be written.
 */
bool el_log_open_(const el_logger *logger, el_log_level level, el_log_open_record_ *open);

/*
 * End the record begun by el_log_open_(), on the sink that began it. `fit`
 * is EL_OK if the message is whole, EL_ERR_NO_SPACE if it is cut. A record
 * the sink does not take is lost, and the sink counts it: nothing is
 * returned.
 */
void el_log_close_(el_log_open_record_ *open, el_status fit);

#endif /* ELEMENTAL_LOG_PRIVATE_H */
