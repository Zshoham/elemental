#include "tee_sink.h"

static el_status tee_write(void *ctx, const el_log_record *record)
{
    const tee_sink *const tee = ctx;
    /* Both get the record, whatever the first one makes of it. */
    const el_status first = el_log_sink_write(tee->first, record);
    const el_status second = el_log_sink_write(tee->second, record);
    return first != EL_OK ? first : second;
}

static const el_log_sink_ops tee_ops = {
    .write = tee_write,
};

void tee_sink_init(tee_sink *tee, el_log_sink first, el_log_sink second)
{
    tee->first = first;
    tee->second = second;
}

el_log_sink tee_sink_as_sink(tee_sink *tee)
{
    return (el_log_sink){.ctx = tee, .ops = &tee_ops};
}
