#include "refusing_sink.h"

#include <stddef.h>

/* Refused when it begins: `end` is never called, but a handle has both. */
static el_status refuse_io(void *ctx, const el_log_record *record, el_buf **message)
{
    (void)ctx;
    (void)record;
    (void)message;
    return EL_ERR_IO;
}

static el_status refuse_ended(void *ctx, const el_log_record *record, el_buf **message)
{
    (void)ctx;
    (void)record;
    (void)message;
    return EL_ERR_END_OF_STREAM;
}

static el_status never_ended(void *ctx, const el_log_record *record, el_status fit)
{
    (void)ctx;
    (void)record;
    (void)fit;
    return EL_ERR_IO;
}

static const el_log_sink_ops io_ops = {.begin = refuse_io, .end = never_ended};
static const el_log_sink_ops ended_ops = {.begin = refuse_ended, .end = never_ended};

el_log_sink refusing_sink(el_status status)
{
    return (el_log_sink){.ctx = NULL, .ops = status == EL_ERR_IO ? &io_ops : &ended_ops};
}
