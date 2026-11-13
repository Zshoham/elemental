/* Compiled by the fmt.format_check.accepts test: correct calls produce no
 * format diagnostic. */
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <elemental/fmt_printf.h>

el_status format_check(el_buf *b, uint64_t u64, int32_t i32, uint8_t u8, size_t size, const char *s,
                       int len);

el_status format_check(el_buf *b, uint64_t u64, int32_t i32, uint8_t u8, size_t size, const char *s,
                       int len)
{
    return el_fmt_printf(b, "%" PRIu64 " %08" PRIx64 " %" PRId32 " %" PRIu8 " %zu %c %s %.*s %%",
                         u64, u64, i32, u8, size, 'c', s, len, s);
}
