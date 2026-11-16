#include "refusing_sink.h"

#include <stddef.h>

static el_status refuse_io(void *ctx, const el_log_record *record)
{
    (void)ctx;
    (void)record;
    return EL_ERR_IO;
}

static el_status refuse_ended(void *ctx, const el_log_record *record)
{
    (void)ctx;
    (void)record;
    return EL_ERR_END_OF_STREAM;
}

static const el_log_sink_ops io_ops = {.write = refuse_io};
static const el_log_sink_ops ended_ops = {.write = refuse_ended};

el_log_sink refusing_sink(el_status status)
{
    return (el_log_sink){.ctx = NULL, .ops = status == EL_ERR_IO ? &io_ops : &ended_ops};
}
