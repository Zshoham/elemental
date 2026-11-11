# Proposal: bytes and crc

Status: Draft
Author, date: design draft for milestone M3, 2026-11-11

There are two small pure modules here, reviewed together, as
[M3](../roadmap.md#m3--formatting-bytes-crc) allows:

- **`bytes`** loads and stores fixed-width integers in a stated byte order,
  through pointers that need no alignment.
- **`crc`** computes CRC-32 and one CRC-16, incrementally.

Their plan entries are in
[module-plan.md](../module-plan.md#added-by-this-plan).

## Problem

`frame` (M6) and the UART framing example put integers on the wire and
check integrity. Any parser of a binary protocol has the same needs.

ISO C has no byte-order functions. POSIX `htonl` covers only big-endian
values up to 32 bits, and it converts values rather than reading
from bytes. Freestanding targets have nothing. So every project writes
shifts by hand, or casts a `uint8_t *` to `uint32_t *`. That cast is
undefined behaviour, and on ARMv6-M it faults at an unaligned address.

Every project also copies a CRC from somewhere, often without being able to
say which of the many CRC-16s it is.

## Usage examples

Both examples were checked on 2026-11-11 with Clang 23.1.4
(`-std=c11 -Wall -Wextra -Wpedantic -fsyntax-only`) against the header
sketches below. They were compiled, not run.

### Example 1: encode and check a record (the common case)

```c
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <elemental/bytes.h>
#include <elemental/crc.h>

/* On the wire: le16 type | le16 length | payload | le32 CRC-32 of
 * everything before it. */
enum { REC_HEAD = 4, REC_TAIL = 4 };

static size_t record_encode(uint8_t *out, size_t cap, uint16_t type,
                            const uint8_t *payload, uint16_t len)
{
    size_t total = REC_HEAD + (size_t)len + REC_TAIL;
    if (total > cap) {
        return 0;
    }
    el_bytes_store_le16(out, type);
    el_bytes_store_le16(out + 2, len);
    memcpy(out + REC_HEAD, payload, len);
    el_bytes_store_le32(out + REC_HEAD + len,
                        el_crc32_update(EL_CRC32_INIT, out, REC_HEAD + len));
    return total;
}

static bool record_check(const uint8_t *in, size_t n, uint16_t *type,
                         const uint8_t **payload, uint16_t *len)
{
    if (n < REC_HEAD + REC_TAIL) {
        return false;
    }
    uint16_t l = el_bytes_load_le16(in + 2);
    if (l > n - REC_HEAD - REC_TAIL) {
        return false;
    }
    /* in + REC_HEAD + l is usually not 4-byte aligned. It does not need
     * to be. */
    if (el_crc32_update(EL_CRC32_INIT, in, REC_HEAD + l) !=
        el_bytes_load_le32(in + REC_HEAD + l)) {
        return false;
    }
    *type = el_bytes_load_le16(in);
    *payload = in + REC_HEAD;
    *len = l;
    return true;
}
```

### Example 2: a CRC over chunks from a non-blocking reader (the awkward case)

A fixed-size, big-endian sensor message arrives in pieces over a UART. The
CRC is updated as bytes arrive, so a message is never walked twice. The
64-bit timestamp sits at offset 3, which is never aligned.

```c
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <elemental/bytes.h>
#include <elemental/crc.h>
#include <elemental/io.h>

/* u8 kind | be16 seq | be64 time_us | be16 CRC-16/IBM-3740 of the rest */
enum { MSG_LEN = 13 };

typedef struct rx {
    uint8_t msg[MSG_LEN];
    size_t have;
    uint16_t crc; /* CRC of msg[0..have), including any CRC bytes seen */
} rx;

static void rx_reset(rx *r)
{
    r->have = 0;
    r->crc = EL_CRC16_IBM3740_INIT;
}

/* EL_OK with *got set when a whole, intact message was decoded. Any other
 * status comes from the reader; EL_ERR_WOULD_BLOCK keeps the state, so call
 * again later. */
static el_status rx_poll(rx *r, el_reader in, bool *got, uint16_t *seq,
                         uint64_t *time_us)
{
    size_t n;

    *got = false;
    el_status st = el_read(in, r->msg + r->have, MSG_LEN - r->have, &n);
    if (st != EL_OK) {
        return st;
    }
    r->crc = el_crc16_ibm3740_update(r->crc, r->msg + r->have, n);
    r->have += n;
    if (r->have < MSG_LEN) {
        return EL_OK;
    }
    /* A message followed by its own CRC, stored big-endian, has a CRC of
     * 0 (the catalogue's residue), so the trailer needs no special case. */
    if (r->crc == 0) {
        *seq = el_bytes_load_be16(r->msg + 1);
        *time_us = el_bytes_load_be64(r->msg + 3);
        *got = true;
    }
    rx_reset(r);
    return EL_OK;
}
```

## Backend example

There is no backend. Both modules are computation on caller memory. A
hardware CRC unit is the application's optimisation
([module-plan.md](../module-plan.md#added-by-this-plan)). The application
can call it directly and test it against `el_crc32_update` as an oracle.

## Why not something that exists

| Candidate | What is insufficient |
|---|---|
| POSIX `htonl`/`ntohs` | Big-endian only, at most 32 bits, value-to-value rather than from bytes, and not on freestanding targets. |
| glibc `<endian.h>` (`htole64`, `be32toh`) | Not ISO C or POSIX.1-2008, and value-to-value: the unaligned load is still the caller's problem. It is used here as the host test oracle. |
| Zephyr `sys/byteorder.h` (`sys_get_le32`, `sys_put_be64`) | *Source fact* (Zephyr 4.2.0): `static inline` and Zephyr only. An inline-only header gives Rust and Python nothing to link ([module-plan.md](../module-plan.md#added-by-this-plan)). |
| Pigweed `pw_bytes/endian.h` (`ReadInOrder`, `CopyInOrder`) | *Source fact* (main at `65ff9fe`): C++17 templates; no C API. |
| Zephyr `sys/crc.h` | *Source fact* (Zephyr 4.2.0): `crc16_ccitt()` is the reflected CRC-16/KERMIT, and `crc16_itu_t()` is the non-reflected one. Which catalogue CRC you get depends on the seed you pass. That ambiguity is the one this proposal names away. Zephyr only. |
| Pigweed `pw_checksum` | *Source fact* (main at `65ff9fe`): `pw_checksum_Crc16Ccitt` is IBM-3740 (init `0xFFFF`, not reflected, table-driven), and `pw_checksum_Crc32Append` chains finished CRC-32 values. That is the same shape and the same variant proposed here. It is a Pigweed module with its own build system. |
| zlib `crc32()` | Same CRC-32 and same chaining convention. It is a separate library, has no CRC-16, and is not freestanding-ready. It is used here as the host test oracle. |

## Dependencies and cost

| | `bytes` | `crc` |
|---|---|---|
| Elemental modules | `status`, for `EL_ASSERT` only (open question 3) | `status`, for `EL_ASSERT` only |
| Runtime symbols (freestanding) | None | None |
| Allocation | None | None |
| Per-object storage | No objects | No objects. The running CRC is one `uint32_t` or `uint16_t` the caller keeps. |
| New status codes | None; nothing fails | None; nothing fails |

**Measured** on 2026-11-11 with Clang 23.1.4, `-Os -ffreestanding
-ffunction-sections -fdata-sections`, for `thumbv7m` (ARMv7-M) and `thumbv6m`
(ARMv6-M). Sizes are `.text` and `.rodata` from `llvm-size -A` per function,
excluding `.ARM.exidx`. None of the variants has an undefined symbol. Each
variant produced the catalogue check values on the host, and gave the same
result when the input was split into two calls. The prototypes are scratch
files and are not kept.

| Implementation | CRC-32 code + table, ARMv7-M / ARMv6-M | CRC-16 code + table, ARMv7-M / ARMv6-M | Steps per byte |
|---|---|---|---|
| Bitwise | 64 / 48 B | 52 / 48 B | 8 |
| 4-bit table (16 entries) | 68 / 56 B + 64 B | 64 / 60 B + 32 B | 2 |
| 8-bit table (256 entries) | 42 / 40 B + 1024 B | 42 / 40 B + 512 B | 1 |

No speed was measured here. *Source fact* (Pigweed `pw_checksum/docs.rst`,
main at `65ff9fe`): for CRC-32 over a 123-character string on an STM32F429,
the 8-bit, 4-bit, and 1-bit tables took 1538, 2153, and 7690 cycles.

The `bytes` functions compiled to byte loads and shifts with no undefined
symbols on both cores. On ARMv7-M, Clang combines a little-endian 32-bit
load into one `ldr`, which that core allows at unaligned addresses.

## Contracts

### Header sketch: `<elemental/bytes.h>`

```c
/* Load a value stored in the given byte order at `src`, which may have any
 * alignment. Cannot fail. */
uint16_t el_bytes_load_le16(const void *src);
uint32_t el_bytes_load_le32(const void *src);
uint64_t el_bytes_load_le64(const void *src);
uint16_t el_bytes_load_be16(const void *src);
uint32_t el_bytes_load_be32(const void *src);
uint64_t el_bytes_load_be64(const void *src);

/* Store `value` in the given byte order at `dst`, which may have any
 * alignment. Writes exactly 2, 4, or 8 bytes. Cannot fail. */
void el_bytes_store_le16(void *dst, uint16_t value);
void el_bytes_store_le32(void *dst, uint32_t value);
void el_bytes_store_le64(void *dst, uint64_t value);
void el_bytes_store_be16(void *dst, uint16_t value);
void el_bytes_store_be32(void *dst, uint32_t value);
void el_bytes_store_be64(void *dst, uint64_t value);
```

These are real functions in `bytes.c`, not `static inline`, so that bindings
have symbols to call. A caller who wants them inlined uses LTO. Pointers are
`void *` so that `char` and `uint8_t` buffers both pass without a cast.

What is left out, for want of a consumer: signed variants (cast the
result), 24- and 48-bit widths, native-order and byte-swap functions, and a
parsing cursor. For a cursor, use `el_mem_reader` until a consumer shows
that a non-virtual one matters.

### Header sketch: `<elemental/crc.h>`

```c
enum {
    /* The CRC of no bytes. Start every computation from it. */
    EL_CRC32_INIT = 0,
    EL_CRC16_IBM3740_INIT = 0xFFFF
};

/*
 * CRC-32/ISO-HDLC, the catalogue's "CRC-32": poly 0x04C11DB7, init
 * 0xFFFFFFFF, reflected in and out, xorout 0xFFFFFFFF, check 0xCBF43926.
 * Used by PKZIP, PNG, and HDLC, among others the catalogue lists.
 *
 * `crc` is the finished CRC of everything before `data`: EL_CRC32_INIT at
 * the start, then the previous return value. Returns the finished CRC of
 * everything so far. Splitting the input differently gives the same result.
 * `data` may be null if `len` is 0. Cannot fail.
 */
uint32_t el_crc32_update(uint32_t crc, const void *data, size_t len);

/*
 * CRC-16/IBM-3740, also called CRC-16/CCITT-FALSE: poly 0x1021, init
 * 0xFFFF, not reflected, xorout 0x0000, check 0x29B1. NOT the reflected
 * CRC-16/KERMIT, which the catalogue calls "CRC-16/CCITT".
 *
 * Same chaining rule as el_crc32_update. A message followed by its CRC
 * stored big-endian has a CRC of 0.
 */
uint16_t el_crc16_ibm3740_update(uint16_t crc, const void *data, size_t len);
```

**One function per CRC, chaining finished values.** The start value is the
CRC of no bytes, and every return value is already final. So one-shot use is
`el_crc32_update(EL_CRC32_INIT, p, n)`, and incremental use feeds the
result back. No state struct is needed, and no `init` or `final` function.
This is the convention of zlib's `crc32()`, Zephyr's `crc32_ieee_update()`,
and Pigweed's `pw_checksum_Crc32Append()`. For CRC-32 it means inverting at
the start and end of each call: two instructions per call, not per byte.

**Why IBM-3740 for the CRC-16** (open question 1), from the
[catalogue](https://reveng.sourceforge.io/crc-catalogue/16.htm), retrieved
2026-11-11, last updated 2024-12-11:

| Variant | init / refin / xorout | check | Aliases and uses (catalogue) |
|---|---|---|---|
| **IBM-3740** | `FFFF` / false / `0000` | `29B1` | CCITT-FALSE, AUTOSAR; floppy disc formats |
| KERMIT | `0000` / true / `0000` | `2189` | CCITT, CCITT-TRUE, BLUETOOTH, V-41-LSB |
| XMODEM | `0000` / false / `0000` | `31C3` | ACORN, LTE, V-41-MSB, ZMODEM |
| IBM-SDLC | `FFFF` / true / `FFFF` | `906E` | X-25, ISO-HDLC, CRC-B; HDLC, T.30 |

- **A non-zero init catches leading zero bytes.** With init 0 (KERMIT,
  XMODEM), zero bytes in front of a message do not change the CRC, so a
  UART that delivers a spurious `0x00` before a frame goes unnoticed.
  Checked with Python's `binascii.crc_hqx`: `"123456789"` with 0, 1, and 2
  leading zeros gives `0x31C3` each time with init 0, and `0x29B1`,
  `0xD0FA`, `0xE5CC` with init `0xFFFF`.
- **Not reflected, and xorout 0.** The bitwise form is MSB-first and needs no
  bit reversal. The residue is 0, which Example 2 relies on.
- **The common embedded "CRC-16/CCITT" in practice.** Pigweed's
  `Crc16Ccitt` is this variant. Zephyr computes it as
  `crc16_itu_t(0xFFFF, ...)`, which serves as an on-target cross-check.
- **IBM-SDLC** is the strongest alternative if interoperating with
  HDLC/PPP/X.25 framing matters. No consumer in the plan needs that.

**The name carries the variant.** `el_crc16_update` would invite exactly
the confusion above, and the catalogue's bare "CRC-16" is a third variant,
CRC-16/ARC. A second CRC-16, if one ever comes, gets its own catalogue
name. `el_crc32` needs no suffix, because the catalogue's "CRC-32" is
ISO-HDLC.

Contracts common to both modules:

- **Ownership and lifetime:** inputs are borrowed for the call. Nothing is
  kept.
- **Errors:** none. Every function returns its result or `void`
  ([conventions.md](../conventions.md#errors), rule 3).
- **Concurrency:** pure functions, safe from any context, concurrently.
  The constant table, if configured, is read-only.
- **Execution context:** ISR-safe. Nothing blocks, allocates, or locks.
- **Contract violations (asserted):** a null pointer for `bytes`; a null
  `data` with `len > 0` for `crc`. A length or alignment is never a
  violation.
- **Exceptions to [conventions.md](../conventions.md):** none. Names are
  `el_bytes_<verb>_<order><width>` and `el_crc<width>[_<variant>]_update`.
  The module prefix is kept, and the variant follows it.

## Interface model

Plain functions (the first and preferred mechanism in
[ADR 0002](../adr/0002-interface-model.md): logic that takes its inputs as
arguments and calls nothing). There is no handle and no object.

## Configuration

| Option | Default | Use case | Why not the linker / a parameter |
|---|---|---|---|
| `BYTES` | y | Enables the module | — |
| `CRC` | y | Enables the module | — |
| choice: `CRC_TABLE_4BIT` / `CRC_TABLE_8BIT` | `CRC_TABLE_4BIT` (open question 2) | 4-bit: 90–130 B per CRC, for small flash. 8-bit: about 1.4× faster (Pigweed's figures) for high-rate links, at 1.5 KiB of tables. | The linker cannot choose between two implementations of one symbol. A parameter would put a speed and size trade-off into every call site and every ABI. Results are identical, so it is private under [ADR 0004](../adr/0004-private-configuration.md). |

Bitwise is not offered. Against the 4-bit table it saves 45–75 B per CRC
(measured above), for four times the steps per byte, and no consumer has asked for that
trade.

## Verification

**Published vectors** for `crc`:

- The catalogue's check values for `"123456789"`: `0xCBF43926`
  (CRC-32/ISO-HDLC) and `0x29B1` (CRC-16/IBM-3740)
  ([16.htm](https://reveng.sourceforge.io/crc-catalogue/16.htm),
  [17plus.htm](https://reveng.sourceforge.io/crc-catalogue/17plus.htm)).
- The CRC of no bytes: `EL_CRC32_INIT` and `EL_CRC16_IBM3740_INIT`.
- The IBM-3740 residue: a message plus its big-endian CRC gives 0.
- A table of lengths 0 to 300 over a fixed pattern. It is generated once
  with Python's `zlib.crc32(data)` and `binascii.crc_hqx(data, 0xFFFF)`,
  which reproduce the catalogue check values (checked 2026-11-11, Python
  3.14.9), and checked into the tests, so the target run needs no Python.
- Incremental: every split point of a 64-byte input gives the one-shot
  result.
- Both Kconfig choices are run against the same vectors. A second test
  executable compiles the CRC sources with the other choice.
- On a Zephyr target, the results are also compared with Zephyr's
  `crc32_ieee_update()` and `crc16_itu_t(0xFFFF, ...)`.

**For `bytes`:**

- The definition is the vector: `0x0102030405060708` is stored as bytes
  `08 07 … 01` in little-endian and `01 02 … 08` in big-endian, and the same
  holds for the 16- and 32-bit prefixes.
- Every function is run at offsets 0 to 7 in a buffer, so every
  misalignment is covered. A store must not touch the byte before or after
  it.
- Random values round-trip.
- On the host, results are compared with glibc's `htole64` and `be32toh`.
- The `sanitize` preset's UBSan reports any misaligned wide access the
  implementation slips in.

**Everywhere:**

- Death tests for the null-pointer violations.
- The tests are broken on purpose to check what they catch: a swapped byte
  order, a wrong table entry, a missing final inversion.
- The Zephyr symbol check (ARMv7-M and ARMv6-M) must show no undefined
  symbols.
- The Rust sketch calls `el_crc32_update` and `el_bytes_load_be64`.

| Target | Toolchain | Level intended |
|---|---|---|
| Linux x86-64 | GCC, Clang, sanitizers | Run |
| Zephyr, ARMv7-M QEMU board | Zephyr SDK | Symbol check, emulator run |
| Zephyr, ARMv6-M QEMU board | Zephyr SDK | Symbol check, emulator run |

## Evidence required before stable

- `frame` (M6) uses `bytes` for its length prefix, and the UART example uses
  `crc`. Then one consumer that was not written to demonstrate them uses
  them too.
- The 4-bit and 8-bit choices are measured on hardware, in cycles per byte
  on one ARMv6-M or ARMv7-M board. Until then the speed claim is Pigweed's,
  not ours.

## Open questions

These are for the owner:

1. Which CRC-16 (IBM-3740 recommended), and whether the variant name goes in
   the function name.
2. Which table choice is the default, and whether bitwise is offered.
3. Whether `bytes` asserts non-null pointers. Asserting adds a dependency on
   `status`, which module-plan currently lists as none. It costs a compare
   and a branch per call. Not asserting means a null load on a Cortex-M
   without an MPU silently reads the vector table at address 0.
4. Whether to chain finished values, or to use init/update/final with a
   state struct.

## Review: three seats

**Application author.**

| Friction found | Fixed by |
|---|---|
| Passing `uint8_t *` and `char *` buffers needed casts. | `void *` parameters. |
| "Which CRC-16 is this?" could not be answered from a name. | The catalogue name is in the function name, and the header gives the parameters and check value. |
| Incremental CRC needed an init, an update, and a finalise, plus a struct to keep. | Chained finished values: one `uint16_t` or `uint32_t` kept, and one start constant. |
| Checking a received CRC meant excluding the trailer from the running CRC. | The documented IBM-3740 residue: feed every byte and compare with 0 (Example 2). |
| Storing a CRC needed a temporary. | Each store takes the value directly, so the call nests (Example 1). |

**Backend author.** There is no backend. This seat was read as a porter to a
new target, and as someone wiring a hardware CRC unit.

| Friction found | Fixed by |
|---|---|
| A hardware CRC unit cannot be plugged in. | Deliberate: the plan makes it the application's optimisation. `el_crc*_update` is its test oracle. |
| Would ARMv6-M need helpers? | Measured: no undefined symbols for any variant on either core. |
| `static inline` would be faster for `bytes`. | Real functions, for bindings; LTO recovers the speed. |

**Maintainer.**

| Friction found | Fixed by |
|---|---|
| A Kconfig option has to justify itself. | It survives the four questions: the linker cannot choose, a parameter would leak, it is not a toolchain property, and the consumer never sees it. Bitwise was dropped for lack of a use case. |
| Two configurations to test. | A second test executable compiles the CRC sources with the other choice, so one preset covers both. |
| Risk of `bytes` widening into a cursor. | Out of scope, with the revisit condition from module-plan. |
| Hard to change later: the CRC-16 choice, the chaining convention, and the names. | All three are open questions for the owner now. |

libedit check: no name here equals one in libedit 3.1 (20260512)
`histedit.h`. Every name has an `el_bytes_` or `el_crc` prefix, and libedit
has neither.
