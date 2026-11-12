# fmt

Text formatting into caller memory: integers in decimal and hex, strings, and
hex dumps, through typed calls or a printf subset. No floating point.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Target | `elemental::fmt` |
| Depends on | `status`. Runtime: `memcpy`; `snprintf` with `FMT_LIBC`. |
| Allocates | No |
| Kconfig | `FMT`; `FMT_BUILTIN` / `FMT_LIBC` ([below](#configuration)) |

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

**No division helper.** With `FMT_BUILTIN`, decimal output divides by 10 with shifts and adds,
because `/` on a `uint64_t` needs `__aeabi_uldivmod` on 32-bit Arm, and even
a 32-bit `/` needs `__aeabi_uidiv` on ARMv6-M. The library is compiled
with `-fno-jump-tables`, because GCC for ARMv6-M at `-Os` turns the printf
parser's dispatch into a jump table that calls libgcc's
`__gnu_thumb1_case_uqi`. The Zephyr
symbol check (ARMv7-M and ARMv6-M) shows `memcpy` as the only symbol needed
([build.md](../../docs/build.md#the-symbol-check)).

## Configuration

A private choice of what turns a number into digits. Output, statuses, and
truncation are identical either way ([ADR 0004](../../docs/adr/0004-private-configuration.md)),
and the whole test suite runs against both.

| Choice | What converts | Use it when |
|---|---|---|
| `FMT_BUILTIN` (default) | Shifts and adds in `src/fmt_digits_builtin.c`: no division helper, no C library beyond `memcpy` | Anything that does not already link printf |
| `FMT_LIBC` | The C library's `snprintf` (`%llu`, `%llx`, `%llX`) into a 21-byte scratch buffer, in `src/fmt_digits_libc.c` | The image links printf anyway, for logging say, and the built-in conversion would be a second copy |

Only the digits come from `snprintf`. The sign, the width and its padding,
and the check that the whole number fits stay in `src/fmt.c`, shared by both
choices, so the item rules cannot differ. (`snprintf` takes its width as an
`int` and its output would have to fit the scratch buffer; an `el_fmt` width
is a `size_t`.) Text items, `%s`, and `%c` never go through the C library,
and `el_fmt_printf` still parses and checks every format itself.

`FMT_LIBC` depends on the probed `PLATFORM_SNPRINTF`: a hosted C library
(CMake finds `snprintf` in `<stdio.h>`), or on Zephyr a picolibc whose
printf level converts `long long` (`CONFIG_PICOLIBC_IO_LONG_LONG` or
`CONFIG_PICOLIBC_IO_FLOAT`; the first is Zephyr v4.5's default, because
`CBPRINTF_FULL_INTEGRAL` is). Selecting it without the probe fails
configuration.

What it costs, built for `mps2/an385` (Cortex-M3) by the Zephyr SDK's GCC
14.3.0 at `-Os`, assertions trapping:

| | `FMT_BUILTIN` | `FMT_LIBC` |
|---|---|---|
| `libelemental_fmt.a` text | 1 705 bytes | 1 502 bytes |
| of which the digit conversion | 290 | 87 |
| Image that already links picolibc's printf (Zephyr defaults; `printk` uses it) | 13 604 | 13 500 (−104) |
| Image with nothing else that formats (`PRINTK`, `CONSOLE`, `LOG` off) | 9 960 | 11 228 (+1 268) |

The images are a `main()` that calls `el_fmt_dec_i64` and `el_fmt_hex_u64`
(text bytes of `zephyr.elf`), with `CONFIG_PICOLIBC_IO_LONG_LONG`. Most of
what the second image gains is picolibc's `__l_vfprintf` (1 156 bytes) and
`snprintf` (84). So `FMT_LIBC` pays off only where printf is already linked.

Not part of the choice: `FMT_LIBC` inherits the C library's guarantees for
`snprintf`. glibc does not document `snprintf` as async-signal-safe, so the
"fine from an interrupt handler" note in `fmt.h` rests on that C library.
picolibc's `snprintf` into a string takes no lock and does not allocate.

## Tests

| Test | What it checks | Where |
|---|---|---|
| `fmt.unit.*` | Every core call and every part of the printf subset against literal expectations: boundaries of capacity, width, and value, whole-or-absent numbers, whole-byte dumps, sticky truncation, termination. | Host and Zephyr |
| `fmt.oracle.*` | The printf subset against glibc's `snprintf`, at every capacity from 0 up, by the prefix rule; decimal conversion against the compiler's division for two million values. | Host only |
| `fmt.violations.*` | Each documented contract violation stops the program, including every conversion, flag, and modifier outside the subset. | Host only (death tests) |
| `fmt.format_check.accepts`, `.rejects` | `EL_FMT_PRINTF_CHECK` lets correct calls compile with `-Werror=format` and rejects a mismatched argument with a format diagnostic. | Host only |

All of them run with either choice: `gcc` and `clang` build `FMT_BUILTIN`,
`sanitize` and the Zephyr `variant` scenario build `FMT_LIBC`
(`configs/variant_defconfig`).

Seen red: deliberately broken copies of the divide, the sticky flag, the
whole-or-absent check, the terminator, and the printf parser were each
caught; so were a `FMT_LIBC` with swapped hex cases, a cut scratch buffer, a
dropped digit, and a whole-or-absent check that lets a number overrun or
copies part of it (see [docs/build.md](../../docs/build.md#what-is-verified)).

## Known gaps

No floating point, no `%p`, no left alignment or `+` flag, no `*` width:
each waits for a consumer. No way to learn the length an unlimited buffer
would have needed, and no resuming after truncation
([ADR 0011](../../docs/adr/0011-formatting-truncation.md), "Revisit when").
The cycle cost of decimal output on ARMv6-M has not been measured.
