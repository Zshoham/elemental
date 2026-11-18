#include <elemental/fmt_printf.h>

void report(int level, const char *format, ...) EL_FMT_PRINTF_CHECK(2, 3);

void caller(void)
{
#if CHECK_THIS_CALL
    report(1, LOCAL_FORMAT, 1.0);
#endif
}
