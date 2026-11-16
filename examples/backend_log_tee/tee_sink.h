/*
 * Example backend: a log sink that gives every record to two others.
 *
 * It is how one logger reaches two destinations: lines on a console and
 * records kept in memory, say. It is also why a sink's operation returns a
 * status although a log call does not: a sink that drives other sinks
 * needs to know what they did.
 *
 * Storage: the caller provides the object and keeps it, and the two sinks
 * behind it, alive while it or any handle made from it is in use. Nothing
 * to destroy. It adds no state of its own, so it is as safe to share
 * between contexts as the two sinks are.
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
} tee_sink;

void tee_sink_init(tee_sink *tee, el_log_sink first, el_log_sink second);

/*
 * A sink handle for `tee`. Each record is given to `first` and then to
 * `second`, whatever `first` made of it. The write returns EL_OK when both
 * took the record; otherwise the first failure: `first`'s status if it
 * failed, else `second`'s.
 */
el_log_sink tee_sink_as_sink(tee_sink *tee);

#ifdef __cplusplus
}
#endif

#endif /* TEE_SINK_H */
