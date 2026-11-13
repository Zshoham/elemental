/*
 * Shared by the printf sources; not installed. The entry points have
 * already checked their arguments, so these functions check nothing.
 */
#ifndef ELEMENTAL_FMT_PRIVATE_H
#define ELEMENTAL_FMT_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>

#include <elemental/io_buf.h>

/*
 * Where printf writes: the buffer the bytes go into, and how to make room
 * in it. el_fmt_printf passes el_buf_reserve on the buffer itself;
 * el_fmt_printf_buffered passes el_buffered_writer_reserve on the writer
 * that borrows it, in its own object file, so a program that prints only
 * into buffers links no buffered-writer code.
 */
typedef struct el_fmt_target_ {
    el_buf *buf;
    el_status (*reserve)(void *ctx, size_t min);
    void *ctx;
} el_fmt_target_;

/* The parser: format into `target` by the item rule (fmt_printf.h). `args`
 * is advanced past every argument that was read. */
el_status el_fmt_format_(const el_fmt_target_ *target, const char *format, va_list *args);

#endif /* ELEMENTAL_FMT_PRIVATE_H */
