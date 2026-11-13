/*
 * Shared by the printf sources; not installed. The entry points have
 * already checked their arguments, so these functions check nothing.
 */
#ifndef ELEMENTAL_FMT_PRIVATE_H
#define ELEMENTAL_FMT_PRIVATE_H

#include "elemental_config.h"

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

/* ---- One parsed specification ------------------------------------------- */

enum el_fmt_length_ {
    EL_FMT_LENGTH_NONE_,
    EL_FMT_LENGTH_HH_,
    EL_FMT_LENGTH_H_,
    EL_FMT_LENGTH_L_,
    EL_FMT_LENGTH_LL_,
    EL_FMT_LENGTH_Z_,
    EL_FMT_LENGTH_J_,
    EL_FMT_LENGTH_T_,
    EL_FMT_LENGTH_BIG_L_
};

/* Flags in a specification. */
enum {
    EL_FMT_FLAG_ZERO_ = 1,
    EL_FMT_FLAG_MINUS_ = 2,
    EL_FMT_FLAG_PLUS_ = 4,
    EL_FMT_FLAG_SPACE_ = 8,
    EL_FMT_FLAG_HASH_ = 16
};

/* Filled in field by field: GCC for ARMv6-M turns `= {0}` on it into a
 * memset() call, which the Zephyr symbol check rejects. */
typedef struct el_fmt_spec_ {
    unsigned flags;
    size_t width;
    int width_star;
    int has_precision;
    int precision_star;
    size_t precision;
    enum el_fmt_length_ length;
    char conversion;
} el_fmt_spec_;

/*
 * Part of a text item: `len` bytes from `s`, or `len` spaces if `s` is null,
 * cut at the last byte that fits. EL_OK, or the target's status with the
 * bytes that fit written.
 */
el_status el_fmt_put_run_(const el_fmt_target_ *target, const char *s, size_t len);

/* Room for `min` bytes, asking the target only when the buffer lacks it. */
el_status el_fmt_need_(const el_fmt_target_ *target, size_t min);

#if defined(EL_CONFIG_FMT_PRINTF_LIBC)
/*
 * FMT_PRINTF_LIBC, in fmt_printf_libc.c: one specification outside the
 * portable subset that the option accepts, its arguments read. Returns 0,
 * reading nothing, for one it does not accept (a contract violation).
 */
int el_fmt_extended_(const el_fmt_target_ *target, const el_fmt_spec_ *s, va_list *args,
                     el_status *status);
#endif

#endif /* ELEMENTAL_FMT_PRIVATE_H */
