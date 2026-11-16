#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/fmt_printf.h>
#include <elemental/log_writer.h>

/*
 * The line without its newline, as far as it fits: printf cuts by its item
 * rule, and the message is cut at a byte. EL_ERR_NO_SPACE if the prefix was
 * cut. A cut message fills the storage, which the caller sees.
 */
static el_status format_line(el_buf *line, const el_log_record *record)
{
    static const char letters[] = "DIWE";

    uint32_t us;
    const uint64_t seconds = el_ticks_to_seconds(record->timestamp, record->ticks_per_second, &us);
    EL_TRY(el_fmt_printf(line, "[%6llu.%06u] %c ", (unsigned long long)seconds, (unsigned)us,
                         letters[record->level]));
    if (record->tag[0] != '\0') {
        EL_TRY(el_fmt_printf(line, "%s: ", record->tag));
    }

    const size_t room = el_buf_room(line);
    const size_t part = record->len < room ? record->len : room;
    return el_buf_append(line, record->message, part);
}

static el_status writer_sink_write(void *ctx, const el_log_record *record)
{
    el_log_writer_sink *const sink = ctx;
    el_buf *const line = &sink->line;
    el_buf_truncate(line, 0);

    el_status status = format_line(line, record);

    /* The newline always goes in, at the cost of a full line's last byte:
     * storage with no room left for it held a line that was too long. */
    if (el_buf_room(line) == 0 && el_buf_len(line) > 0) {
        el_buf_truncate(line, el_buf_len(line) - 1);
        status = EL_ERR_NO_SPACE;
    }
    /* Fails only with no storage at all, which the prefix already reported. */
    el_status_ignore(el_buf_append(line, "\n", 1));

    const el_status written = el_write_all(sink->out, el_buf_data(line), el_buf_len(line), NULL);
    if (written != EL_OK) {
        status = written;
    }
    if (status != EL_OK) {
        sink->lost++;
    }
    return status;
}

static const el_log_sink_ops writer_sink_ops = {
    .write = writer_sink_write,
};

void el_log_writer_sink_init(el_log_writer_sink *sink, el_writer out, void *line_storage,
                             size_t line_cap)
{
    EL_ASSERT(sink != NULL);
    EL_ASSERT(out.ops != NULL);
    sink->out = out;
    /* Checks the storage: null only with a capacity of 0. */
    el_buf_init(&sink->line, line_storage, line_cap);
    sink->lost = 0;
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
