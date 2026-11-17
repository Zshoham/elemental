#include "tee_sink.h"

static el_status tee_begin(void *ctx, const el_log_record *record, el_buf **message)
{
    (void)record;
    tee_sink *const tee = ctx;
    /* The message is collected here, then passed on to both. */
    el_buf_truncate(tee->message, 0);
    *message = tee->message;
    return EL_OK;
}

static el_status tee_end(void *ctx, const el_log_record *record, el_status fit)
{
    const tee_sink *const tee = ctx;
    /* Both get the record, whatever the first one makes of it. */
    const el_status first = el_log_sink_write(tee->first, record);
    const el_status second = el_log_sink_write(tee->second, record);
    if (first != EL_OK) {
        return first;
    }
    return second != EL_OK ? second : fit;
}

static const el_log_sink_ops tee_ops = {
    .begin = tee_begin,
    .end = tee_end,
};

void tee_sink_init(tee_sink *tee, el_log_sink first, el_log_sink second, el_buf *message)
{
    tee->first = first;
    tee->second = second;
    tee->message = message;
}

el_log_sink tee_sink_as_sink(tee_sink *tee)
{
    return (el_log_sink){.ctx = tee, .ops = &tee_ops};
}
