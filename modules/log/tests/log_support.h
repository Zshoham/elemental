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
 * called: the last record, a copy of its message, and what pointer the
 * message was.
 */
typedef struct recording_sink {
    el_status answer;
    unsigned calls;
    el_log_record last;
    const char *last_message_pointer;
    char message[1200];
    void *last_ctx;
} recording_sink;

void recording_sink_init(recording_sink *sink);
el_log_sink recording_sink_as_sink(recording_sink *sink);

/* Handles that were not made properly. */
el_log_sink recording_sink_without_write(recording_sink *sink);

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
