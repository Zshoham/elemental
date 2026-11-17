#ifndef STRESS_SUPPORT_H
#define STRESS_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/log.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The sink the stress test drains into. It expects the records the test
 * logs: message "record <i> " followed by i % 17 times 'x', time i + 1 at
 * 1000 ticks a second, level info, tag "stress"; and each i greater than
 * the one before. It counts the records and the ones that are not that,
 * and keeps the first of those.
 */
typedef struct sequence_sink {
    size_t count;
    size_t wrong;
    long previous;
    char first_wrong[64]; /* the message, terminated */
    el_buf buf;
    char storage[64];
} sequence_sink;

void sequence_sink_init(sequence_sink *sink);
el_log_sink sequence_sink_as_sink(sequence_sink *sink);

#ifdef __cplusplus
}
#endif

#endif /* STRESS_SUPPORT_H */
