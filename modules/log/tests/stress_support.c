#include "stress_support.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static el_status sequence_begin(void *ctx, const el_log_record *record, el_buf **message)
{
    (void)record;
    sequence_sink *const sink = ctx;
    el_buf_truncate(&sink->buf, 0);
    *message = &sink->buf;
    return EL_OK;
}

/* The record number, if the record is one the test logs; -1 otherwise. */
static long expected_record(const el_log_record *record, el_status fit)
{
    char text[sizeof(((sequence_sink *)0)->storage) + 1];
    static const char prefix[] = "record ";
    const size_t prefix_len = sizeof(prefix) - 1;
    if (fit != EL_OK || record->len < prefix_len || record->len >= sizeof(text)) {
        return -1;
    }
    memcpy(text, record->message, record->len);
    text[record->len] = '\0';
    if (memcmp(text, prefix, prefix_len) != 0) {
        return -1;
    }
    char *rest;
    const long number = strtol(text + prefix_len, &rest, 10);
    if (rest == text + prefix_len || number < 0 || *rest != ' ') {
        return -1;
    }
    rest++;
    const size_t tail = (size_t)number % 17;
    if (strlen(rest) != tail || strspn(rest, "x") != tail) {
        return -1;
    }
    const bool fields = record->timestamp == (el_ticks)number + 1 &&
                        record->ticks_per_second == 1000 && record->level == EL_LOG_LEVEL_INFO &&
                        strcmp(record->tag, "stress") == 0;
    return fields ? number : -1;
}

static el_status sequence_end(void *ctx, const el_log_record *record, el_status fit)
{
    sequence_sink *const sink = ctx;
    sink->count++;
    const long number = expected_record(record, fit);
    if (number <= sink->previous) {
        if (sink->wrong == 0) {
            const size_t kept = record->len < sizeof(sink->first_wrong) - 1
                                    ? record->len
                                    : sizeof(sink->first_wrong) - 1;
            if (kept > 0) {
                memcpy(sink->first_wrong, record->message, kept);
            }
            sink->first_wrong[kept] = '\0';
        }
        sink->wrong++;
    } else {
        sink->previous = number;
    }
    return EL_OK;
}

static const el_log_sink_ops sequence_ops = {.begin = sequence_begin, .end = sequence_end};

void sequence_sink_init(sequence_sink *sink)
{
    sink->count = 0;
    sink->wrong = 0;
    sink->previous = -1;
    sink->first_wrong[0] = '\0';
    el_buf_init(&sink->buf, sink->storage, sizeof(sink->storage));
}

el_log_sink sequence_sink_as_sink(sequence_sink *sink)
{
    return (el_log_sink){.ctx = sink, .ops = &sequence_ops};
}
