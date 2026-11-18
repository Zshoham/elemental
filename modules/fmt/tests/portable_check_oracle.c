/* Run the same literal calls through the checker and the builtin parser. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <elemental/fmt_printf.h>

static int matches(const char *format, el_status status, const el_buf *buf, int length,
                   const char *expected)
{
    if (status == EL_OK && length >= 0 && el_buf_len(buf) == (size_t)length &&
        memcmp(el_buf_data(buf), expected, (size_t)length) == 0) {
        return 1;
    }
    fprintf(stderr, "builtin printf disagrees with snprintf for '%s'\n", format);
    return 0;
}

int main(void)
{
    char expected[128];
    unsigned char storage[128];
    el_buf buf;
    int ok = 1;

#define CHECK(...)                                                           \
    do {                                                                     \
        el_buf_init(&buf, storage, sizeof storage);                          \
        const el_status status = el_fmt_printf(&buf, __VA_ARGS__);           \
        const int length = snprintf(expected, sizeof expected, __VA_ARGS__); \
        ok &= matches(#__VA_ARGS__, status, &buf, length, expected);         \
    } while (0)

    CHECK("%d", -42);
    CHECK("%i", 42);
    CHECK("%u", 42u);
    CHECK("%x", 42u);
    CHECK("%X", 42u);
    CHECK("%4c", 'a');
    CHECK("%8s", "abc");
    CHECK("%.s", "abc");
    CHECK("%.0s", "abc");
    CHECK("%8.2s", "abc");
    CHECK("%8.*s", 2, "abc");
    CHECK("%.*s", -1, "abc");
    CHECK("%08d", -42);
    CHECK("%07i", 42);
    CHECK("%06u", 42u);
    CHECK("%05x", 42u);
    CHECK("%04X", 42u);
    CHECK("%hhd", (signed char)-42);
    CHECK("%hi", (short)-42);
    CHECK("%lu", 42ul);
    CHECK("%llx", 42ull);
    CHECK("%zX", (size_t)42);
    CHECK("%zd", (ptrdiff_t)-42);
    CHECK("%" PRIu64, UINT64_C(42));
    CHECK("mix %%"
          " %d "
          "%"
          ".*s",
          -42, 2, "abc");
    CHECK("%%");
#undef CHECK
    return !ok;
}
