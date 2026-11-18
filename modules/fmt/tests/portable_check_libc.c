#include <inttypes.h>

#include <elemental/fmt_printf.h>

void libc_formats(el_buf *buf, va_list args)
{
    el_status_ignore(el_fmt_vprintf(buf, "%#o %+#08.3x %-*s %*c %.*s %+.*zd", args));
    el_status_ignore(el_fmt_vprintf(buf, "%f %F %e %E %g %G %a %A %lf %La %p %td %ju", args));
    el_status_ignore(el_fmt_vprintf(buf, "%" PRIo64, args));
    el_status_ignore(el_fmt_vprintf(buf, "%n", args));      // reject: '%n'|unsupported
    el_status_ignore(el_fmt_vprintf(buf, "%lc %ls", args)); // reject: '%lc'|length; '%ls'|length
    // reject next: '%Ld'|length; '%llf'|length; '%hp'|length
    el_status_ignore(el_fmt_vprintf(buf, "%Ld %llf %hp", args));
    // reject next: '%+s'|flags; '%.2c'|precision
    el_status_ignore(el_fmt_vprintf(buf, "%+s %.2c", args));
    el_status_ignore(el_fmt_vprintf(buf, "%2147483648f", args)); // reject: INT_MAX
    el_status_ignore(el_fmt_vprintf(buf, "%123d %s %%", args));
}
