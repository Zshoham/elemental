/*
 * elemental/fmt.h - text formatting into caller memory.
 *
 * Integers in decimal and hex, strings, and hex dumps, appended to a buffer
 * the caller owns. The text is then the caller's to send, for example with
 * el_write_all(). Nothing here writes to a stream: a stream can stall
 * halfway through a number, and a formatter cannot resume there.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 *     char line[64];
 *     el_fmt f;
 *     el_fmt_init(&f, line, sizeof line);
 *     el_fmt_cstr(&f, "temp=");
 *     el_fmt_dec_i64(&f, celsius, 0, 0);
 *     if (el_fmt_cstr(&f, " C\n") == EL_ERR_NO_SPACE) {
 *         // line holds what fitted; see "When it does not fit"
 *     }
 *     el_write_all(out, line, el_fmt_len(&f), NULL);
 *
 * A printf-style front end over the same object is in
 * <elemental/fmt_printf.h>.
 *
 * When it does not fit (docs/adr/0011-formatting-truncation.md):
 *
 *   A call returns EL_ERR_NO_SPACE, and the buffer holds a prefix of what
 *   unlimited storage would hold:
 *
 *   - A number (sign, padding, and digits) goes in whole or not at all. A
 *     cut number would read as a different number.
 *   - Text is cut at the last byte that fits. A cut may split a multi-byte
 *     UTF-8 sequence.
 *   - A hex dump is cut after the last whole byte.
 *   - Truncation is sticky. After the first call that could not append all
 *     of its item, nothing more is written and every call returns
 *     EL_ERR_NO_SPACE until el_fmt_init(). So checking the status of the
 *     last call is enough.
 *
 *   To keep a trailing newline even when the text is cut, give el_fmt_init()
 *   one byte less than the buffer, then add the '\n' at buf[el_fmt_len()].
 *
 * Termination: when the capacity is above zero, the buffer is NUL-terminated
 * after every call, el_fmt_init() included. The length never counts the
 * terminator, so at most cap - 1 bytes of text fit. With a capacity of zero,
 * nothing is ever written.
 *
 * Statuses, for every function that appends:
 *
 *   EL_OK            The whole item was appended.
 *   EL_ERR_NO_SPACE  This item, or an earlier one, did not fit. What this
 *                      call appended follows the rules above: part of a
 *                      text, whole bytes of a dump, nothing of a number,
 *                      and nothing at all if an earlier call was cut.
 *
 *   An empty item (a zero-length string or dump) returns EL_OK and changes
 *   nothing, unless the object was already cut.
 *
 * Ownership and storage: the caller provides the el_fmt object and the
 * buffer, and keeps the buffer alive while the object is used. Strings and
 * data passed to a call are borrowed for that call. Nothing holds a
 * resource, so nothing needs ending. An el_fmt may be copied at any time;
 * both copies then write into the same buffer.
 *
 * The struct layout is public so that callers can allocate it. The fields
 * are not part of the API: use the functions.
 *
 * Concurrency: no internal synchronization. One object, one context at a
 * time; distinct objects are independent. Nothing blocks or allocates, so
 * use from an interrupt handler is fine under that rule. One exception: a
 * build configured with FMT_LIBC converts numbers with the C library's
 * snprintf(), and is then exactly as safe in an interrupt or signal handler
 * as that snprintf() is. glibc does not promise async-signal safety;
 * picolibc's string snprintf() takes no lock and does not allocate.
 *
 * Contract violations: a null object; a null buffer with a nonzero
 * capacity; a null string or data with a nonzero length; a null C string;
 * flags other than the ones each function lists.
 */
#ifndef ELEMENTAL_FMT_H
#define ELEMENTAL_FMT_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/status.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el_fmt {
    char *buf;
    size_t cap;
    size_t len;       /* text written, not counting the terminator */
    el_status status; /* EL_OK until an item did not fit */
} el_fmt;

/* Flags for the number functions. Combine with |. */
enum {
    /* Pad to the width with '0' after any sign, instead of with ' ' before
     * it. */
    EL_FMT_ZERO_PAD = 1,
    /* Hex digits A-F instead of a-f. el_fmt_hex_u64() only. */
    EL_FMT_UPPER = 2
};

/*
 * Start formatting into the `cap` bytes at `buf`. `buf` may be null only if
 * `cap` is 0. Writes the terminator at buf[0] when cap > 0. Calling it again
 * on the same object starts over.
 */
void el_fmt_init(el_fmt *f, char *buf, size_t cap);

/* Bytes of text written so far, not counting the terminator. */
size_t el_fmt_len(const el_fmt *f);

/*
 * Append `len` bytes of text, cut at the last byte that fits. The bytes are
 * copied as they are, NUL included. `s` may be null if `len` is 0.
 */
el_status el_fmt_str(el_fmt *f, const char *s, size_t len);

/* Append a NUL-terminated string, cut at the last byte that fits. */
el_status el_fmt_cstr(el_fmt *f, const char *s);

/*
 * Append an integer, in decimal or in hex, whole or not at all.
 *
 * `width` is a minimum: shorter output is right-aligned with spaces, or,
 * with EL_FMT_ZERO_PAD, filled with '0' after the sign. A negative value
 * has a '-'; nothing else gets a sign or a prefix ("0x" is the caller's to
 * write). The output equals C's printf for "%*" PRIu64, "%0*" PRId64,
 * "%*" PRIx64, and so on.
 *
 * Flags: EL_FMT_ZERO_PAD for all three; EL_FMT_UPPER for hex only.
 */
el_status el_fmt_dec_u64(el_fmt *f, uint64_t value, size_t width, uint32_t flags);
el_status el_fmt_dec_i64(el_fmt *f, int64_t value, size_t width, uint32_t flags);
el_status el_fmt_hex_u64(el_fmt *f, uint64_t value, size_t width, uint32_t flags);

/*
 * Append `len` bytes as two lower-case hex digits each, separated by one
 * space: "de ad be ef". No space comes before the first byte or after the
 * last. Cut after the last whole byte that fits. `data` may be null if `len`
 * is 0.
 */
el_status el_fmt_hex_bytes(el_fmt *f, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_FMT_H */
