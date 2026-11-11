# fmt

Text formatting into caller memory: integers in decimal and hex, strings, and
hex dumps, through typed calls or a printf subset. No floating point.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Target | `elemental::fmt` |
| Depends on | `status`. Runtime: `memcpy`. |
| Allocates | No |
| Kconfig | `FMT` |

## What is here

| Piece | Header |
|---|---|
| `el_fmt`, the cursor over a buffer, and the typed calls: `el_fmt_str`, `el_fmt_cstr`, `el_fmt_dec_u64`, `el_fmt_dec_i64`, `el_fmt_hex_u64`, `el_fmt_hex_bytes` | [`elemental/fmt.h`](include/elemental/fmt.h) |
| `el_fmt_printf`, `el_fmt_vprintf`, `EL_FMT_PRINTF_CHECK`: the printf subset, for C and C++ callers | [`elemental/fmt_printf.h`](include/elemental/fmt_printf.h) |

The design and its review are in the
[proposal](../../docs/proposals/fmt.md). What a buffer that is too small
holds is [ADR 0011](../../docs/adr/0011-formatting-truncation.md): numbers
whole or absent, text cut at the last byte, truncation sticky, and always a
terminator.

## Using it

```c
char line[64];
el_fmt f;
el_fmt_init(&f, line, sizeof line);
el_fmt_printf(&f, "%" PRIu32 " frames, last ", count);
el_fmt_hex_bytes(&f, frame, frame_len);
if (el_fmt_cstr(&f, "\n") == EL_ERR_NO_SPACE) {
    /* line holds a prefix that ends on a whole item */
}
el_write_all(out, line, el_fmt_len(&f), NULL);
```

Only the last call's status needs checking. To keep a newline at the end of
a cut line, give `el_fmt_init` one byte less than the buffer and add the
`'\n'` afterwards. Bindings to other languages call the typed core; the
printf header is a C convenience (`tests/interop/rust_sketch.rs` does this).

**No division helper.** Decimal output divides by 10 with shifts and adds,
because `/` on a `uint64_t` needs `__aeabi_uldivmod` on 32-bit Arm, and even
a 32-bit `/` needs `__aeabi_uidiv` on ARMv6-M. The library is compiled
with `-fno-jump-tables`, because GCC for ARMv6-M at `-Os` turns the printf
parser's dispatch into a jump table that calls libgcc's
`__gnu_thumb1_case_uqi`. The Zephyr
symbol check (ARMv7-M and ARMv6-M) shows `memcpy` as the only symbol needed
([build.md](../../docs/build.md#the-symbol-check)).

## Tests

| Test | What it checks | Where |
|---|---|---|
| `fmt.unit.*` | Every core call and every part of the printf subset against literal expectations: boundaries of capacity, width, and value, whole-or-absent numbers, whole-byte dumps, sticky truncation, termination. | Host and Zephyr |
| `fmt.oracle.*` | The printf subset against glibc's `snprintf`, at every capacity from 0 up, by the prefix rule; decimal conversion against the compiler's division for two million values. | Host only |
| `fmt.violations.*` | Each documented contract violation stops the program, including every conversion, flag, and modifier outside the subset. | Host only (death tests) |
| `fmt.format_check.accepts`, `.rejects` | `EL_FMT_PRINTF_CHECK` lets correct calls compile with `-Werror=format` and rejects a mismatched argument with a format diagnostic. | Host only |

Seen red: deliberately broken copies of the divide, the sticky flag, the
whole-or-absent check, the terminator, and the printf parser were each
caught (see [docs/build.md](../../docs/build.md#what-is-verified)).

## Known gaps

No floating point, no `%p`, no left alignment or `+` flag, no `*` width:
each waits for a consumer. No way to learn the length an unlimited buffer
would have needed, and no resuming after truncation
([ADR 0011](../../docs/adr/0011-formatting-truncation.md), "Revisit when").
The cycle cost of decimal output on ARMv6-M has not been measured.
