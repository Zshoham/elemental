#include <elemental/fmt_printf.h>

template <class T, class U> el_buf *get_buffer(T, U);

void check_cxx(const char *runtime, int number)
{
    // reject next: '%f'|conversion
    el_status_ignore(el_fmt_printf((get_buffer<int, int>(1, 2)), R"test(%f)test", 1.0));
    el_status_ignore(el_fmt_printf(nullptr,
                                   R"(a "quote" \x25 and %% and
# 9 "inside a raw string"
)"
                                   "%d",
                                   0));
    el_status_ignore(el_fmt_printf(nullptr, R"(%% %f)", 1.0)); // reject: '%f'|conversion
    el_status_ignore(el_fmt_printf(nullptr, runtime, 1'000));
    // reject next: '%f'|conversion
    el_status_ignore(el_fmt_printf(nullptr,
                                   "\x25"
                                   "f",
                                   number));
}
