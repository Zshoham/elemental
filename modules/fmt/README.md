# fmt

Integers to text, and a printf subset that formats into an `io` buffer or a
buffered writer. A number is whole or absent. No floating point.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Target | `elemental::fmt` |
| Depends on | `status`, `io`. Runtime: `memcpy`. |
| Allocates | No; printf into grown storage grows it through its allocator |
| Kconfig | `FMT` (selects `IO`) |

## What is here

| Piece | Header |
|---|---|
| `el_fmt_dec_u64`, `el_fmt_dec_i64`, `el_fmt_hex_u64`, `EL_FMT_INT_LEN_MAX`, `EL_FMT_UPPER`: pure conversions into a caller's scratch array | [`elemental/fmt.h`](include/elemental/fmt.h) |
| `el_fmt_printf`, `el_fmt_vprintf` into an `el_buf`; `el_fmt_printf_buffered`, `el_fmt_vprintf_buffered` into an `el_buffered_writer`; `EL_FMT_PRINTF_CHECK`: for C and C++ callers | [`elemental/fmt_printf.h`](include/elemental/fmt_printf.h) |

The design is the [io buffers and fmt proposal](../../docs/proposals/io-buffers-fmt.md),
which revised the [first one](../../docs/proposals/fmt.md) in milestone M3a.
What a target that is too small holds is
[ADR 0015](../../docs/adr/0015-formatting-into-io-buffers.md): numbers whole
or absent, text cut at the last byte, nothing after the first item that did
not fit within a call, no terminator.

## Using it

A line formatted once into a fixed buffer, then sent:

```c
uint8_t storage[96];
el_buf line;
el_buf_init(&line, storage, sizeof storage);
el_status st = el_fmt_printf(&line, "%10" PRIu64 " %s: ", ticks, level);
if (st == EL_OK) {
    st = el_fmt_printf(&line, "%.*s", (int)len, message); /* chain: not sticky */
}
el_write_all(out, el_buf_data(&line), el_buf_len(&line), NULL);
```

Straight to a non-blocking stream, through a buffered writer
(`elemental/io_buffered.h`): printf streams, passing buffered bytes on when
the buffer cannot take the next item, and stops at an item if the stream
stalls. Reserving a line first makes it all-or-nothing:

```c
if (el_buffered_writer_reserve(&console, LINE_MAX) == EL_OK) {
    (void)el_fmt_printf_buffered(&console, "%8" PRIu32 " %" PRId32 " mC\n", up, temp);
}
(void)el_buffered_writer_flush(&console);
```

Bindings to other languages call the conversions and `el_buf_append`; the
printf header is a C convenience (`tests/interop/rust_sketch.rs` does this).

**No division helper.** Decimal output divides by 10 with shifts and adds,
because `/` on a `uint64_t` needs `__aeabi_uldivmod` on 32-bit Arm, and even
a 32-bit `/` needs `__aeabi_uidiv` on ARMv6-M. The library is compiled with
`-fno-jump-tables`, because GCC for ARMv6-M at `-Os` turns the printf
parser's dispatch into a jump table that calls libgcc's
`__gnu_thumb1_case_uqi`, and the parser's specification record is filled in
field by field, because `= {0}` on it becomes a `memset` call there. The
Zephyr symbol check (ARMv7-M and ARMv6-M) shows `memcpy` and `memmove`
([build.md](../../docs/build.md#the-symbol-check)).

**What links.** `fmt_printf.c` and `fmt_printf_buffered.c` are separate
objects, and growth is reached through the buffer's hook: a program that
prints only into fixed buffers links no allocator and no buffered-writer
code.

## Configuration

None of its own beyond `FMT`. The earlier digit option, `FMT_BUILTIN` /
`FMT_LIBC`, was removed in M3a; the platform's formatter comes back as an
option on printf ([proposal](../../docs/proposals/io-buffers-fmt.md#the-platform-option-on-printf)).

## Tests

| Test | What it checks | Where |
|---|---|---|
| `fmt.unit.*` | The conversions at their boundaries; every part of the printf subset into a buffer against literal expectations; the item rule, per call; printf into a buffered writer: streaming, a stall stopping at an item, capacity 0, an ended stream; the proposal's Examples 1 and 2 (Example 2 against a scripted UART that stalls). | Host and Zephyr |
| `fmt.unit.grown.*` | printf into grown storage: Examples 3 and 2b. Only with `IO_ALLOC`. | Host and Zephyr |
| `fmt.oracle.*` | The subset against glibc's `snprintf`: into a buffer at every capacity, and through an 8-byte buffered writer in front of a stream that stalls after k bytes, for every k; decimal conversion against the compiler's division for two million values. | Host only |
| `fmt.violations.*` | Each documented contract violation stops the program, including every conversion, flag, and modifier outside the subset. | Host only (death tests) |
| `fmt.format_check.accepts`, `.rejects` | `EL_FMT_PRINTF_CHECK` lets correct calls compile with `-Werror=format` and rejects a mismatched argument with a format diagnostic. | Host only |

## Known gaps

No floating point, no `%p`, no left alignment or `+` flag, no `*` width.
No way to learn the length an unlimited buffer would have needed, and no
resuming inside a call
([ADR 0015](../../docs/adr/0015-formatting-into-io-buffers.md), "Revisit
when"). The cycle cost of decimal output on ARMv6-M has not been measured.
