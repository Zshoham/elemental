/*
 * Example backend: a log sink that gives every record to two others.
 *
 * It is how one logger reaches two destinations: lines on a console and
 * records kept in memory, say. It is also why a sink's operation returns a
 * status although a log call does not: a sink that drives other sinks
 * needs to know what they did.
 *
 * A sink lends the buffer a record's message is written into
 * (elemental/log.h), and two sinks cannot share one: the tee lends a
 * buffer of its own, and passes the message on to each sink from there. A
 * message that buffer cannot take whole is passed on cut, and the tee
 * reports EL_ERR_NO_SPACE for it.
 *
 * Storage: the caller provides the object and the message buffer, fixed or
 * grown, and keeps them, and the two sinks behind it, alive while it or any
 * handle made from it is in use. Nothing to destroy. The buffer is state:
 * one tee, one context at a time.
 */
#ifndef TEE_SINK_H
#define TEE_SINK_H

#include <elemental/log.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tee_sink {
    el_log_sink first;
    el_log_sink second;
    el_buf *message; /* borrowed */
} tee_sink;

void tee_sink_init(tee_sink *tee, el_log_sink first, el_log_sink second, el_buf *message);

/*
 * A sink handle for `tee`. Each record is given to `first` and then to
 * `second`, whatever `first` made of it. Its end returns EL_OK when both
 * took the record whole; otherwise the first failure: `first`'s status if
 * it failed, else `second`'s, else EL_ERR_NO_SPACE for a cut message.
 */
el_log_sink tee_sink_as_sink(tee_sink *tee);

#ifdef __cplusplus
}
#endif

#endif /* TEE_SINK_H */
