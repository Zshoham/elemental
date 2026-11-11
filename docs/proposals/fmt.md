# Proposal: fmt

Status: Accepted (2026-11-11, by the project owner)
Author, date: design draft for milestone M3, 2026-11-11

Formats text into caller memory: integers, strings, and hex dumps. It has a
non-variadic core of typed calls and a printf-subset convenience in its own
header. The plan entry is in
[module-plan.md](../module-plan.md#named-by-the-owner), and the milestone is
[M3](../roadmap.md#m3--formatting-bytes-crc).

## Problem

The `log` writer sink (M4) has to turn a record into one line of text in a
stack buffer, and then send it with `el_write_all`. Applications have the
same need for console output and diagnostics on targets where `snprintf` is
missing, too large, or unpredictable. Today they use `snprintf`, which
brings in floating point and locale code. On newlib-nano it also drops `%ll`
unless the application enables it. Its count-or-negative return breaks
[ADR 0003](../adr/0003-status-and-progress.md), and a truncated number in
its output reads as a different number. Or they write a `utoa` by hand,
and on ARMv6-M that hand-written `utoa` pulls in `__aeabi_uldivmod`.

## Usage examples

Both examples were checked on 2026-11-11 with Clang 23.1.4
(`-std=c11 -Wall -Wextra -Wpedantic -fsyntax-only`) against the header
sketches below. They were compiled, not run.

### Example 1: a log line into a stack buffer (the common case)

This is how M4's writer sink could emit one record. The record's fields
belong to this example; M4 designs the real ones.

```c
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <elemental/fmt.h>
#include <elemental/fmt_printf.h>
#include <elemental/io.h>

typedef struct line_sink {
    el_writer out;
    uint32_t truncated; /* records that did not fit the line buffer */
} line_sink;

static el_status line_sink_emit(line_sink *sink, uint64_t ticks,
                                const char *level, const char *msg,
                                size_t msg_len)
{
    char line[96];
    el_fmt f;

    /* Hold one byte back so that a truncated line still ends in '\n'. */
    el_fmt_init(&f, line, sizeof line - 1);
    el_fmt_printf(&f, "%10" PRIu64 " %s: ", ticks, level);
    if (el_fmt_str(&f, msg, msg_len) == EL_ERR_NO_SPACE) {
        sink->truncated++; /* still written: a cut line beats no line */
    }

    size_t len = el_fmt_len(&f);
    line[len++] = '\n';
    return el_write_all(sink->out, line, len, NULL);
}
```

Only the last call's status is checked. Truncation is sticky, so a status
from the last call covers every call before it. The text is a prefix of the
untruncated line, and the number is never cut. With `cap` 95 (94 bytes of
text and the NUL), a long message gives
`"      1234 warn: " + the first 77 bytes of msg + "\n"`, 95 bytes in all.

### Example 2: a hex dump through a small console buffer (the awkward case)

A received frame is dumped 16 bytes per line, after a summary line with a
signed value and a status name. It uses only the core, which is what Rust
and other bindings see.

```c
#include <stddef.h>
#include <stdint.h>

#include <elemental/fmt.h>
#include <elemental/io.h>
#include <elemental/status.h>

static el_status dump_frame(el_writer console, const uint8_t *frame,
                            size_t len, int32_t rssi_dbm, el_status rx)
{
    char line[64];
    el_fmt f;

    el_fmt_init(&f, line, sizeof line);
    el_fmt_cstr(&f, "rx ");
    el_fmt_cstr(&f, el_status_name(rx));
    el_fmt_cstr(&f, " rssi=");
    el_fmt_dec_i64(&f, rssi_dbm, 0, 0);
    el_fmt_cstr(&f, " len=");
    el_fmt_dec_u64(&f, len, 0, 0);
    el_fmt_cstr(&f, "\n");
    el_status st = el_write_all(console, line, el_fmt_len(&f), NULL);

    /* "0010: de ad be ef ...": 6 + 47 + 1 = 54 bytes, so a line always fits. */
    for (size_t off = 0; off < len && st == EL_OK; off += 16) {
        size_t n = (len - off < 16) ? len - off : 16;

        el_fmt_init(&f, line, sizeof line); /* reuse: start again */
        el_fmt_hex_u64(&f, off, 4, EL_FMT_ZERO_PAD);
        el_fmt_cstr(&f, ": ");
        el_fmt_hex_bytes(&f, frame + off, n);
        el_fmt_cstr(&f, "\n");
        st = el_write_all(console, line, el_fmt_len(&f), NULL);
    }
    return st;
}
```

With a 24-byte buffer, the first dump line would hold
`"0000: 01 02 03 04 05 06"` (23 characters and a NUL), and the remaining
calls would return `EL_ERR_NO_SPACE`. A byte is never cut in half, and no
separator is left dangling.

From Rust, the core is plain C calls, and the formatted bytes are the slice
`buf[..el_fmt_len(&f)]`:

```rust
let mut buf = [0u8; 32];
let mut f = MaybeUninit::<ElFmt>::uninit();
unsafe {
    el_fmt_init(f.as_mut_ptr(), buf.as_mut_ptr().cast(), buf.len());
    let st = el_fmt_dec_u64(f.as_mut_ptr(), u64::MAX, 0, 0);
    assert_eq!(st, EL_OK);
    let n = el_fmt_len(f.as_ptr());
    assert_eq!(&buf[..n], b"18446744073709551615");
}
```

## Backend example

There is no backend. Formatting is computation on caller memory, and
nothing in it varies by platform. Variation by target is covered by the
Zephyr symbol check and the target test run, not by an interface.

## Why not something that exists

| Candidate | What is insufficient |
|---|---|
| `snprintf` (C11 7.21.6.5) | It discards characters past `n - 1` and returns a count or a negative value, so a number can be cut mid-digit and the result breaks [ADR 0003](../adr/0003-status-and-progress.md). It brings floating point and locale code. It is not guaranteed on a freestanding target. It is variadic, so Rust cannot call it portably. |
| `el_mem_writer` | Using it adds an `io` dependency, which [module-plan.md](../module-plan.md#3-decisions-that-must-come-first) ("Where stream glue lives") rules out. It cuts at byte granularity and reports `EL_ERR_END_OF_STREAM`, not `EL_ERR_NO_SPACE`. It has no idea of an item, so it cannot leave a number out whole. |
| Zephyr `cbprintf` | Zephyr only. *Source fact* (Zephyr 4.2.0, `lib/os/cbprintf_complete.c` lines 30–35 and 791–807): with `CONFIG_CBPRINTF_FULL_INTEGRAL` it converts a `uintmax_t` with `value % radix` and `value /= radix`. Its shift-and-multiply `_ldiv5` is used only for floating point. |
| Pigweed `pw_string` | C++ only. *Source fact* (`pw_string/type_to_string.h`, main at `65ff9fe`): `IntToString` writes nothing but a NUL when the digits do not fit, and returns `ResourceExhausted`. That is the same "never half a number" rule proposed here. It does use 64-bit division, by 10⁹, at most twice per value. |

## Dependencies and cost

| | |
|---|---|
| Elemental modules | `status` (codes and `EL_ASSERT`) |
| Runtime symbols (freestanding) | `memcpy` only, in `el_fmt_str`. There is no `/` or `%` on a non-constant or 64-bit operand, and no division at all (see below). |
| Allocation | None |
| Per-object storage | `el_fmt`: four words (16 bytes on a 32-bit target). The text buffer is the caller's. |
| New status codes | None. `EL_ERR_NO_SPACE` is already admitted by [ADR 0009](../adr/0009-capacity-status.md). |

**Decimal without a division helper.** Measured on 2026-11-11 with Clang
23.1.4, `-ffreestanding`, compiled for `thumbv6m` (ARMv6-M) and `thumbv7m`
(ARMv7-M) at `-Os` and `-O2`, by reading the undefined symbols with
`llvm-nm -u`. The scratch files are not kept in the tree.

| Digit loop | ARMv7-M `-Os` | ARMv7-M `-O2` | ARMv6-M `-Os` and `-O2` |
|---|---|---|---|
| `uint64_t v; v % 10; v /= 10` | `__aeabi_uldivmod` | none | `__aeabi_uldivmod`, `__aeabi_lmul` |
| `uint32_t v; v % 10; v /= 10` | none | none | `__aeabi_uidiv` |
| `(uint64_t)a * b` (reciprocal multiply, as in Zephyr's `_ldiv5`) | none | none | `__aeabi_lmul` |
| Shift-and-add divide by 10 on `uint64_t` | none | none | none |

So on ARMv6-M even a 32-bit `/ 10` needs a helper, and whether ARMv7-M does
depends on the optimisation level. The implementation therefore divides by
10 with shifts and adds only (Hacker's Delight, `divu10`, with one more
`q += q >> 32` step for 64 bits). It uses a `uint32_t` form while the value
fits, because that form is cheaper on 32-bit cores. The 64-bit form was
checked against `/` and `%` on the host for 10⁸ values, including every
power of two and ten ±3 and values near `UINT64_MAX`, with no mismatch. Hex
needs only shifts and masks. The cycle cost per digit has not been
measured.

A second risk: GCC can turn a padding loop into a call to `memset`
(`-ftree-loop-distribute-patterns`), which is not on the allowlist. Clang 23
did not do this for a plain fill loop. The Zephyr symbol check (ARMv7-M and
ARMv6-M) on the GCC-based Zephyr toolchain is what will show it. If it
happens, the fix is local: write the padding as part of the digit loop.

## Contracts

### Header sketch: `<elemental/fmt.h>`

```c
typedef struct el_fmt {
    char *buf;
    size_t cap;
    size_t len;       /* text written, excluding the NUL */
    el_status status; /* EL_OK until the first item that did not fit */
} el_fmt;

enum {
    EL_FMT_ZERO_PAD = 1, /* pad to width with '0' after any sign, not ' ' */
    EL_FMT_UPPER = 2     /* hex digits A-F; el_fmt_hex_u64 only */
};

/* Start formatting into the `cap` bytes at `buf`. `buf` may be null only if
 * `cap` is 0. Writes buf[0] = '\0' when cap > 0. Cannot fail. Calling it
 * again on the same object starts over. */
void el_fmt_init(el_fmt *f, char *buf, size_t cap);

/* Text written so far, not counting the terminator: buf[0..len). */
size_t el_fmt_len(const el_fmt *f);

/* Text, cut at the last byte that fits. `s` may be null if `len` is 0. */
el_status el_fmt_str(el_fmt *f, const char *s, size_t len);
/* The same for a NUL-terminated string. */
el_status el_fmt_cstr(el_fmt *f, const char *s);

/* Numbers: all of it, or none of it. `width` is a minimum; shorter output
 * is right-aligned with spaces, or with zeros after the sign when
 * EL_FMT_ZERO_PAD is set. No prefix ("0x") and no '+' are written. */
el_status el_fmt_dec_u64(el_fmt *f, uint64_t value, size_t width, uint32_t flags);
el_status el_fmt_dec_i64(el_fmt *f, int64_t value, size_t width, uint32_t flags);
el_status el_fmt_hex_u64(el_fmt *f, uint64_t value, size_t width, uint32_t flags);

/* Bytes as two lower-case hex digits each, separated by one space:
 * "de ad be ef". Cut after the last whole byte that fits. */
el_status el_fmt_hex_bytes(el_fmt *f, const void *data, size_t len);
```

### Header sketch: `<elemental/fmt_printf.h>`

```c
#include <stdarg.h>
#include <elemental/fmt.h>

#if defined(__GNUC__) /* GCC and Clang */
#define EL_FMT_PRINTF_CHECK(format_arg, first_arg) \
    __attribute__((format(printf, format_arg, first_arg)))
#else
#define EL_FMT_PRINTF_CHECK(format_arg, first_arg)
#endif

/* Appends to `f`, like the core calls. Same truncation rule; each
 * conversion is an item, and each run of literal text is a text item. */
el_status el_fmt_printf(el_fmt *f, const char *format, ...) EL_FMT_PRINTF_CHECK(2, 3);
el_status el_fmt_vprintf(el_fmt *f, const char *format, va_list args)
    EL_FMT_PRINTF_CHECK(2, 0);
```

**The subset** is the smallest one that formats every fixed-width type
through `<inttypes.h>`, and strings that come with a length:

| Part | Accepted |
|---|---|
| Conversions | `d i u x X c s %` |
| Flags | `0` (integers only) |
| Width | Decimal digits. `*` is not accepted. |
| Precision | For `s` only: `.N` and `.*`. `%.*s` is how a length-delimited string ([conventions.md](../conventions.md#storage-size-and-alignment), rule 4) is printed. |
| Length | `hh h l ll z`. `PRIu32` is `"lu"` and `PRIu64` is `"llu"` on Arm newlib, and `PRIu8` may be `"hhu"`. |

The rule: **for every format it accepts, the output is exactly what C's
`snprintf` writes with unlimited space.** Anything else (`%f`, `%p`, `%n`,
the flags `- + #` and space, `j t L`, precision on integers) is a contract
violation. It is asserted. With assertions compiled out, formatting stops at that conversion
and the call returns `EL_OK`. Nothing past it is read from `args`, so a
mismatched argument list cannot be read out of bounds.

The format attribute checks argument *types* against full printf. It does
not reject the conversions outside the subset, because `%f` is valid printf.
The runtime assertion covers those.

`EL_FMT_PRINTF_CHECK` is public because `log` (M4) needs it on its own
variadic function. It depends on the compiler, not on configuration, so
[ADR 0004](../adr/0004-private-configuration.md) is not affected.

### Truncation: what "what fitted" means

- **The output is always a prefix** of what unlimited storage would hold.
- **An item** is one core call, one printf conversion, or one run of
  literal text in a printf format.
- **Numbers are whole or absent.** A number's sign, padding, and digits go
  in together or not at all. A value that is cut reads as a different
  value; a value that is missing does not.
- **Text is cut at the last byte that fits.** This covers `el_fmt_str`,
  `el_fmt_cstr`, literal text, and `%s` and `%c` with their padding. A cut
  can split a UTF-8 sequence; Elemental treats text as bytes.
- **A hex dump is cut after the last whole byte**, meaning two digits plus
  the space before them.
- **Truncation is sticky.** After the first item that did not fit whole,
  nothing more is written, and every later call returns `EL_ERR_NO_SPACE`
  until `el_fmt_init`. Without this, a later short item could land after a
  dropped one, and `"count= ok"` would hide that the count is missing.
- **NUL termination (decision 1):** when `cap > 0`, the
  buffer is NUL-terminated after every call, including `el_fmt_init`.
  `len` never counts the terminator, so at most `cap - 1` bytes of text fit.
  With `cap == 0`, nothing is ever written.

### Statuses and outputs

| Function | `EL_OK` | `EL_ERR_NO_SPACE` |
|---|---|---|
| `el_fmt_init`, `el_fmt_len` | Cannot fail. | — |
| `el_fmt_str`, `el_fmt_cstr` | All `len` bytes appended. | This item, or an earlier one, did not fit. As many bytes as fitted were appended, and none if an earlier item was already cut. |
| `el_fmt_dec_*`, `el_fmt_hex_u64` | The whole number, with its padding, was appended. | Nothing was appended by this call. |
| `el_fmt_hex_bytes` | Every byte was appended. | Whole bytes were appended, up to the last one that fits. |
| `el_fmt_printf`, `el_fmt_vprintf` | The whole format was appended. | Items up to the first one that did not fit, by the rules above. |

In every case `len` is the length of what the buffer now holds, and the
buffer is a valid prefix ending in a NUL. A zero-length item on an object
that is not yet truncated returns `EL_OK` and changes nothing.

- **Ownership and lifetime:** the caller owns `el_fmt` and the buffer. The
  object borrows the buffer from `el_fmt_init` until it is last used.
  Strings and data passed to a call are borrowed for that call. Nothing
  needs ending.
- **Errors:** `EL_ERR_NO_SPACE` only, as above.
- **Concurrency:** none internal. One object, one context at a time.
  Distinct objects are independent.
- **Execution context:** never blocks, never allocates, takes no lock. It is
  safe in an interrupt handler under the one-context rule, as `io_mem` is.
- **Contract violations (asserted):** a null `f`; a null `buf` with
  `cap > 0`; a null `s` or `data` with a nonzero length; a null C string
  or format; unknown bits in `flags`; `EL_FMT_UPPER` on a decimal call; a
  conversion outside the subset; a null `%s` argument.
- **Exceptions to [conventions.md](../conventions.md):** none.
  `fmt_printf.h` is the variadic convenience on a non-variadic core that ABI
  rule 5 allows. Its two functions are for C and C++ callers; bindings call
  the core.

## Interface model

Plain functions on a caller-owned object (mechanism 1 of
[ADR 0002](../adr/0002-interface-model.md)). There is no handle, because
nothing varies. Several `el_fmt` objects can coexist, and an `el_fmt` can be
copied at any time, because nothing holds its address.

**Its own cursor, not `el_mem_writer`** (decision 3). The cursor needs to
know about items and a sticky truncation state, which a byte stream does not
have, and using `el_mem_writer` would make `io` a dependency. The cost is a
second "buffer, capacity, length" struct in the library. `fmt` has no `io`
glue: as module-plan says, its glue is one `el_write_all`, and Example 1
shows it.

## Configuration

| Option | Default | Use case | Why not the linker / a parameter |
|---|---|---|---|
| `FMT` | y | Enables the module | — |

There are no other options. Leaving out printf is the linker's job: if
nothing calls `el_fmt_printf`, it is not linked. A "32-bit only" option is
not needed either, because the 32-bit fast path is internal and chosen by
value at run time.

## Verification

- **Unit tests (GoogleTest).** For each function: `cap` 0, 1, exact fit,
  and one byte short. Widths below, at, and above the digit count.
  `INT64_MIN`, `UINT64_MAX`, 0, and each power of ten ±1. Sticky truncation
  across calls. NUL termination after every call. Reuse through
  `el_fmt_init`.
- **printf against an oracle (host only).** A table of accepted formats
  crossed with boundary and random values. When the result fits, the output
  must equal host glibc `snprintf`. When it does not, it must equal the
  prefix rule applied to the `snprintf` result.
- **Decimal against `/` and `%` (host only):** 10⁷ random `uint64_t` values
  per run, plus the boundaries above.
- **Byte-identical on target.** The literal-expectation tests (not the
  oracle tests, because a target libc's printf may differ) run under Zephyr
  on one ARMv7-M and one ARMv6-M QEMU board, as set up in M1.
- **The Zephyr symbol check (ARMv7-M and ARMv6-M):** nothing beyond
  `memcpy`.
- **Format attribute:** a translation unit that passes a `uint64_t` to
  `%d` is built with `-Werror=format` and must fail. It is registered as a
  test that is expected to fail to compile.
- **Death tests** for each violation listed above, in both assertion-handler
  configurations.
- **Rust:** `tests/interop/rust_sketch.rs` gains the snippet from Example 2.
  It is compiled, linked, and run.
- **Mutation:** the shift-and-add divide, the sticky flag, and the
  "number whole or absent" rule are each broken on purpose, and the tests
  must catch each one.

| Target | Toolchain | Level intended |
|---|---|---|
| Linux x86-64 | GCC, Clang, sanitizers | Run |
| Zephyr, ARMv7-M QEMU board | Zephyr SDK | Symbol check, emulator run |
| Zephyr, ARMv6-M QEMU board | Zephyr SDK | Symbol check, emulator run |

## Evidence required before stable

The baseline in [api-stability.md](../process/api-stability.md), and in
addition:

- `log` (M4) formats its writer-sink lines with `fmt`, and a second consumer
  that was not written to demonstrate it uses `fmt` too.
- The printf subset has not grown without a named consumer for each
  addition. Each addition keeps the "same as `snprintf`" rule.
- The cycle cost of decimal conversion on ARMv6-M is measured on hardware,
  or the claim stays unmeasured.

## Decisions

Decided by the project owner on 2026-11-11, accepting each recommendation:

1. **Truncation.** Numbers are whole or absent. Text is cut at the last byte
   that fits. Truncation is sticky. The buffer is always NUL-terminated
   when `cap > 0`.
2. **The printf subset**, exactly as above: `d i u x X c s %`, the `0` flag,
   decimal width, `.N` and `.*` for `%s` only, and `hh h l ll z`.
3. **Its own cursor**, `el_fmt`, not `el_mem_writer`.

The truncation rule is recorded in
[ADR 0011](../adr/0011-formatting-truncation.md).

## Review: three seats

**Application author.**

| Friction found | Fixed by |
|---|---|
| Checking a status after every append made Example 1 twice as long. | Sticky truncation: the last call's status covers all the calls before it. |
| A truncated log line lost its `'\n'`, so the next record joined the same line. | Documented pattern: initialise with `cap - 1` and add the newline after formatting (Example 1). No new function. |
| Literal strings and `el_status_name()` needed a separate `strlen`. | `el_fmt_cstr`. |
| `PRIu64` expands to `"llu"` on Arm and `"lu"` on x86-64, so the subset failed on one of them. | `l` and `ll` accepted; `hh` and `h` accepted for `PRIu8` and `PRIu16` on some libcs. |
| Elemental strings come with a length, but `%s` wants a NUL. | `%.*s`, and `el_fmt_str` in the core. |
| With NUL termination, Example 1 gives up a byte for the NUL and another for the newline. | Accepted cost (decision 1). |

**Backend author.** There is no backend. This seat was read as the person
porting to a new target, and as the M4 sink author who builds on `fmt`.

| Friction found | Fixed by |
|---|---|
| Decimal `uint64_t` pulled `__aeabi_uldivmod` and `__aeabi_lmul` on ARMv6-M, and even 32-bit `/ 10` pulled `__aeabi_uidiv`. | Shift-and-add divide, measured above. |
| The M4 log front end needs format checking on its own variadic function. | `EL_FMT_PRINTF_CHECK` is public. |
| The sink cannot pass a `va_list` through `el_fmt_printf`. | `el_fmt_vprintf`. |
| GCC may emit `memset` for padding. | Left to the Zephyr symbol check, with the local fix named above. |

**Maintainer.**

| Friction found | Fixed by |
|---|---|
| A risk of creep toward full printf. | The subset table, plus the rule that every accepted format matches `snprintf`. The oracle test makes adding a conversion cheap to verify and easy to see in review. |
| A cursor shared with `io` would couple two modules for one struct. | Its own cursor; the only dependency is `status`. |
| `width` as `size_t` allows absurd values. | Not a problem: an absurd width is just `EL_ERR_NO_SPACE`. Nothing multiplies it, so it cannot overflow. |
| Hard to change later: the truncation rule, NUL termination, and the `el_fmt` layout. | The first two were decided by the owner and recorded in ADR 0011. |

Bare `el_<verb>` names: none. Every function is `el_fmt_*`. No name equals
one that libedit 3.1 (20260512) declares in `histedit.h`. The closest is
`el_parse`, which this proposal does not use.
