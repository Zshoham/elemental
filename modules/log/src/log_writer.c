#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/fmt_printf.h>
#include <elemental/log_writer.h>

/*
 * The line's start, as far as it fits: printf cuts by its item rule, and
 * nothing follows a cut item. EL_ERR_NO_SPACE if it was cut.
 */
static el_status format_prefix(el_buf *line, const el_log_record *record)
{
    static const char letters[] = "DIWE";

    uint32_t us;
    const uint64_t seconds = el_ticks_to_seconds(record->timestamp, record->ticks_per_second, &us);
    EL_TRY(el_fmt_printf(line, "[%6llu.%06u] %c ", (unsigned long long)seconds, (unsigned)us,
                         letters[record->level]));
    if (record->tag[0] != '\0') {
        EL_TRY(el_fmt_printf(line, "%s: ", record->tag));
    }
    return EL_OK;
}

static el_status writer_sink_begin(void *ctx, const el_log_record *record, el_buf **message)
{
    el_log_writer_sink *const sink = ctx;
    /* A record already open here is being built in the line this would
     * empty. */
    EL_ASSERT(sink->open == 0);
    sink->open = 1;
    el_buf_truncate(sink->line, 0);
    sink->prefix = format_prefix(sink->line, record);
    sink->prefix_len = el_buf_len(sink->line);
    /* Lent even when the start of the line was cut: the line still goes
     * out, cut, and ends in a newline. */
    *message = sink->line;
    return EL_OK;
}

static el_status writer_sink_end(void *ctx, const el_log_record *record, el_status fit)
{
    (void)record;
    el_log_writer_sink *const sink = ctx;
    el_buf *const line = sink->line;

    el_status status = fit;
    if (sink->prefix != EL_OK) {
        /* Nothing follows a cut item, the message included. */
        el_buf_truncate(line, sink->prefix_len);
        status = sink->prefix;
    }

    /* The newline always goes in, at the cost of the line's last byte: a
     * buffer that cannot take it even by growing held a line too long. An
     * empty line can only be a buffer with no storage at all, which the
     * prefix already reported. */
    if (el_buf_append(line, "\n", 1) != EL_OK && el_buf_len(line) > 0) {
        el_buf_truncate(line, el_buf_len(line) - 1);
        /* Cannot fail: truncating kept that byte's storage. */
        el_status_ignore(el_buf_append(line, "\n", 1));
        status = EL_ERR_NO_SPACE;
    }

    const el_status written = el_write_all(sink->out, el_buf_data(line), el_buf_len(line), NULL);
    if (written != EL_OK) {
        status = written;
    }
    if (status != EL_OK) {
        sink->lost++;
    }
    /* Only now: the line was lent until the write returned, and a callback
     * on that path (the writer, a growing buffer's allocator) that begins a
     * record here would overwrite the line being written. */
    sink->open = 0;
    return status;
}

static const el_log_sink_ops writer_sink_ops = {
    .begin = writer_sink_begin,
    .end = writer_sink_end,
};

void el_log_writer_sink_init(el_log_writer_sink *sink, el_writer out, el_buf *line)
{
    EL_ASSERT(sink != NULL);
    EL_ASSERT(out.ops != NULL);
    EL_ASSERT(line != NULL);
    sink->out = out;
    sink->line = line;
    sink->prefix_len = 0;
    sink->prefix = EL_OK;
    sink->lost = 0;
    sink->open = 0;
}

el_log_sink el_log_writer_sink_as_sink(el_log_writer_sink *sink)
{
    EL_ASSERT(sink != NULL);
    return (el_log_sink){.ctx = sink, .ops = &writer_sink_ops};
}

uint32_t el_log_writer_sink_lost(const el_log_writer_sink *sink)
{
    EL_ASSERT(sink != NULL);
    return sink->lost;
}
