#include <inttypes.h>

#include <elemental/fmt_printf.h>
#include <elemental/log_printf.h>

#define STRING_FORMAT ("%.*s")
#define JOINED_FORMAT "%" PRIu64

void portable_accepts(el_logger *log, el_buf *buf, const char *format, int choose)
{
    EL_LOG(log, EL_LOG_LEVEL_INFO, JOINED_FORMAT, UINT64_C(42));
    EL_LOG_DEBUG(log, STRING_FORMAT, 3, "string");
    EL_LOG_INFO(log, "escaped \045d", 42);
    EL_LOG_WARN(log, "old data\000%f");
    EL_LOG_ERROR(log, "%0003d", 42);
    el_status_ignore((el_fmt_printf)(buf,
                                     ("%"
                                      "d"),
                                     42));
    EL_LOG_ERROR(log, format, 1.0);
    EL_LOG_ERROR(log, choose ? "%f" : "%p", 1.0);
    EL_LOG_ERROR(log, ("%f") + 1, 1.0);
    EL_LOG_ERROR(log, &"%f"[1]);
    el_status_ignore(el_fmt_printf(choose ? (void)0, "%f", buf : buf, format, 1.0));
    /* el_fmt_printf(buf, "%f", 1.0); */
    const char *text = "el_fmt_printf(buf, \"%f\", 1.0)";
    (void)text;
}
