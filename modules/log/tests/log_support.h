#ifndef LOG_SUPPORT_H
#define LOG_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/io.h>
#include <elemental/log.h>
#include <elemental/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A sink that answers what the test tells it to and remembers how it was
 * called: the record as begin saw it, the last record ended, a copy of its
 * message, where the message was, and whether it came cut. It lends `lend`,
 * which is its own buffer unless the test changes it; the buffer's
 * capacity is the test's to set.
 */
typedef struct recording_sink {
    el_status begin_answer;
    el_status answer;
    unsigned begins;
    unsigned calls; /* ends */
    el_log_record begun;
    el_log_record last;
    const char *last_message_pointer;
    el_status last_fit;
    char message[256];
    void *last_ctx;
    el_buf buf;
    el_buf *lend;
    char storage[256];
} recording_sink;

/* Its buffer is all of `storage`. */
void recording_sink_init(recording_sink *sink);
/* Its buffer is the first `cap` bytes of `storage`, emptied. */
void recording_sink_set_capacity(recording_sink *sink, size_t cap);
el_log_sink recording_sink_as_sink(recording_sink *sink);

/* Handles that were not made properly: an operation missing. */
el_log_sink recording_sink_without_begin(recording_sink *sink);
el_log_sink recording_sink_without_end(recording_sink *sink);

/*
 * A sink that takes every record and, each time one ends, writes `again`
 * to `echo`: a destination that logs while it is being drained into. It
 * echoes `limit` times and no more, so that a drain that follows its own
 * echoes still ends.
 */
typedef struct echoing_sink {
    el_log_sink echo;
    el_log_record again;
    unsigned limit;
    unsigned ends;
    el_buf buf;
    char storage[64];
} echoing_sink;

void echoing_sink_init(echoing_sink *sink, el_log_sink echo, const el_log_record *again,
                       unsigned limit);
el_log_sink echoing_sink_as_sink(echoing_sink *sink);

/* A clock that reads what the test wrote into it and counts its readers. */
typedef struct counting_clock {
    el_ticks reading;
    uint32_t ticks_per_second;
    unsigned now_calls;
    unsigned ticks_per_second_calls;
} counting_clock;

void counting_clock_init(counting_clock *clock, uint32_t ticks_per_second);
el_clock counting_clock_as_clock(counting_clock *clock);

/*
 * A writer that keeps what it is given and counts the calls. It takes at
 * most `chunk` bytes per call (0: everything), and once it holds
 * `fail_after` bytes every further call returns `failure` instead.
 */
typedef struct scripted_writer {
    char bytes[512];
    size_t len;
    unsigned calls;
    size_t chunk;
    size_t fail_after;
    el_status failure;
} scripted_writer;

void scripted_writer_init(scripted_writer *writer);
el_writer scripted_writer_as_writer(scripted_writer *writer);

#ifdef __cplusplus
}
#endif

#endif /* LOG_SUPPORT_H */
