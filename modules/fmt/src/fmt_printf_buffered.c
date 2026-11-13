/*
 * printf into a buffered writer: the shared parser, with the writer's
 * reserve as the way to make room. Its own object file, so that a program
 * that prints only into buffers links no buffered-writer code.
 */
#include "elemental_config.h"

#include <stdarg.h>

#include <elemental/assert.h>
#include <elemental/fmt_printf.h>

#include "fmt_private.h"

static el_status writer_reserve(void *ctx, size_t min)
{
    return el_buffered_writer_reserve(ctx, min);
}

/* The writer's buffer is its `buf` field: io's layout, read here because fmt
 * writes into the buffer the writer borrows (docs/proposals/io-buffers-fmt.md,
 * Implementation notes). */
static el_fmt_target_ target_of(el_buffered_writer *out)
{
    const el_fmt_target_ target = {.buf = out->buf, .reserve = writer_reserve, .ctx = out};
    return target;
}

/* A writer whose stream has ended takes nothing, even an item that would
 * fit its buffer: reserving nothing reports it. */
static el_status open_for_items(el_buffered_writer *out)
{
    return el_buffered_writer_reserve(out, 0);
}

el_status el_fmt_vprintf_buffered(el_buffered_writer *out, const char *format, va_list args)
{
    EL_ASSERT(out != NULL);
    EL_ASSERT(format != NULL);
    const el_status open = open_for_items(out);
    if (open != EL_OK) {
        return open;
    }
    const el_fmt_target_ target = target_of(out);
    va_list copy;
    va_copy(copy, args);
    const el_status status = el_fmt_format_(&target, format, &copy);
    va_end(copy);
    return status;
}

el_status el_fmt_printf_buffered(el_buffered_writer *out, const char *format, ...)
{
    EL_ASSERT(out != NULL);
    EL_ASSERT(format != NULL);
    const el_status open = open_for_items(out);
    if (open != EL_OK) {
        return open;
    }
    const el_fmt_target_ target = target_of(out);
    va_list args;
    va_start(args, format);
    const el_status status = el_fmt_format_(&target, format, &args);
    va_end(args);
    return status;
}
