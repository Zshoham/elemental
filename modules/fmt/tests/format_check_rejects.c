/* Compiled by the fmt.format_check.rejects test: a uint64_t passed for %d
 * must be diagnosed by the format attribute on el_fmt_printf(). */
#include <stdint.h>

#include <elemental/fmt_printf.h>

el_status format_check(el_fmt *f, uint64_t u64);

el_status format_check(el_fmt *f, uint64_t u64)
{
    return el_fmt_printf(f, "%d", u64);
}
