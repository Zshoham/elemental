# Proposal: bytes and crc

Status: Accepted (2026-11-11, by the project owner)
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
| Elemental modules | `status`, for `EL_ASSERT` only (decision 3) | `status`, for `EL_ASSERT` only |
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

**Why IBM-3740 for the CRC-16** (decision 1), from the
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
| choice: `CRC_TABLE_4BIT` / `CRC_TABLE_8BIT` | `CRC_TABLE_4BIT` (decision 2) | 4-bit: 90–130 B per CRC, for small flash. 8-bit: about 1.4× faster (Pigweed's figures) for high-rate links, at 1.5 KiB of tables. | The linker cannot choose between two implementations of one symbol. A parameter would put a speed and size trade-off into every call site and every ABI. Results are identical, so it is private under [ADR 0004](../adr/0004-private-configuration.md). |

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

## Decisions

Decided by the project owner on 2026-11-11, accepting each recommendation:

1. **CRC-16/IBM-3740**, with the variant in the function name:
   `el_crc16_ibm3740_update`.
2. **A private Kconfig choice** between 16-entry tables (the default) and
   256-entry tables. There is no bitwise version.
3. **`bytes` asserts non-null pointers**, and so depends on `status`.
4. **CRC calls take the previous result back**, starting from the CRC of
   no bytes. There is no state struct.

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
| Hard to change later: the CRC-16 choice, the chaining convention, and the names. | All three were decided by the owner (Decisions, below). |

libedit check: no name here equals one in libedit 3.1 (20260512)
`histedit.h`. Every name has an `el_bytes_` or `el_crc` prefix, and libedit
has neither.

## Amendment (2026-11-12)

Two additions the owner decided on 2026-11-12. Both keep the module
boundaries above: no new module, no new dependency.

### crc: the platform's CRC behind the same functions

**Decision (owner).** Where the compiler or the platform already computes
these CRCs, offer it as another option of the private Kconfig choice. The
software tables stay the default. Every option gives identical results, so
the choice stays private under
[ADR 0004](../adr/0004-private-configuration.md) and nothing a caller sees
changes.

The choice becomes `CRC_IMPLEMENTATION`, with four options:

| Option | Computes with | Available when |
|---|---|---|
| `CRC_TABLE_4BIT` (default) | 16-entry tables | Always |
| `CRC_TABLE_8BIT` | 256-entry tables | Always |
| `CRC_GCC_BUILTIN` | GCC's `__builtin_rev_crc32_data8` and `__builtin_crc16_data8` | CMake's compile probe finds both builtins |
| `CRC_ZEPHYR` | Zephyr's `crc32_ieee_update()` and `crc16_itu_t()` from `<zephyr/sys/crc.h>` | A Zephyr build with `CONFIG_CRC=y` and no `CONFIG_CRC_HW_HANDLER` |

Availability is a toolchain fact, probed by CMake and passed into Kconfig
the way `PLATFORM_POSIX` is: the root `Kconfig` gains
`COMPILER_GCC_CRC_BUILTINS` and `PLATFORM_ZEPHYR_CRC`, which have no prompt.
An option whose fact is false cannot be selected, and asking for it in a
defconfig stops configuration with the reason.

Facts behind the options, measured or read on 2026-11-12:

- **GCC builtins.** *Measured:* GCC has `__builtin_crc*` and
  `__builtin_rev_crc*` only from GCC 15. The host's GCC 16.2.1 has them; the
  Zephyr SDK's GCC 14.3.0 and Clang 23 do not. On x86-64, even with
  `-mpclmul`, GCC 16 compiles them to a 1.5 KB table, not to a hardware
  instruction. So the option buys nothing yet on the targets that matter. It
  exists so that a GCC that does lower it to an instruction is used without
  a source change, and it is tested where it builds (the host's GCC).
- **Zephyr's `sys/crc.h`.** *Source fact* (Zephyr v4.5.0,
  `subsys/crc/crc32_sw.c`, `subsys/crc/crc16_sw.c`): `crc32_ieee_update(crc,
  …)` inverts on entry and exit and `crc32_ieee()` starts it from 0, which
  is exactly `el_crc32_update` and `EL_CRC32_INIT`. `crc16_itu_t(seed, …)`
  is the MSB-first polynomial 0x1021 with no final XOR, returning the raw
  register, so `crc16_itu_t(0xFFFF, …)` is CRC-16/IBM-3740 and passing the
  result back as the seed continues it, exactly as `el_crc16_ibm3740_update`.
  Both are compiled whenever `CONFIG_CRC=y`; both are `__weak`.
- **Why not with `CONFIG_CRC_HW_HANDLER`.** *Source fact*
  (`subsys/crc/crc_hardware.c`): with a devicetree `zephyr,crc` chosen
  node, Zephyr replaces the weak functions with calls into the CRC driver.
  On a driver error they log and return 0. That breaks "cannot fail" and
  "identical results", and the driver path is not known to be safe in an
  interrupt handler. The probe therefore requires that handler to be off.

**The hardware CRC engine is not built now.** The hardware that matters on
microcontrollers is a CRC peripheral, reached through Zephyr's
device-bound driver API (`<zephyr/drivers/crc.h>`: `crc_begin`,
`crc_update`, `crc_finish` on a `const struct device *` and a
`struct crc_ctx`; STM32, NXP, Renesas RA, Silicon Labs, Ambiq and other
backends in v4.5.0). Each call returns an `int` that can be an error, and it
needs a device. That does not fit `el_crc*_update`, which takes no handle
and cannot fail. It is recorded in
[module-plan.md](../module-plan.md#added-by-this-plan) as a deferred, separate
small interface: a CRC-engine handle with a Zephyr-driver backend, waiting
for a consumer.

**Testing every option.** `configs/variant_defconfig` holds one option per
choice, and keeps `CRC_TABLE_8BIT`: it is the only non-default option that
builds both on the host and on Zephyr. Every option is covered by:

- a generic integration case, `integration.choices`, which enumerates every
  Kconfig `choice` and every option visible on the host's toolchain,
  configures a build with that option selected, builds only the tests of
  the module that owns the choice, and runs them. A future choice is
  covered with no change to the case.
- a Twister scenario, `elemental.tests.crc_zephyr`, on `mps2/an385`, for
  `CRC_ZEPHYR`, which no host can select.

### bytes: byte swap and bit counting

**Decision (owner).** Add byte swap and bit counting to `bytes`. This is
the owner's use case, and it widens "What is left out" above, which listed
byte swap as having no consumer. They go in `bytes` rather than a new
module, following the owner's preference for fewer, broader modules.

```c
/* Reverse the order of the bytes. */
uint16_t el_bytes_swap16(uint16_t value);
uint32_t el_bytes_swap32(uint32_t value);
uint64_t el_bytes_swap64(uint64_t value);

/* Bit counting, after C23 <stdbit.h>, which C11 does not have. Each is
 * defined for every input, 0 included. */
uint32_t el_bytes_leading_zeros32(uint32_t value);  /* 32 for 0 */
uint32_t el_bytes_trailing_zeros32(uint32_t value); /* 32 for 0 */
uint32_t el_bytes_count_ones32(uint32_t value);
bool el_bytes_has_single_bit32(uint32_t value);     /* false for 0 */
uint32_t el_bytes_bit_width32(uint32_t value);      /* 0 for 0 */
uint32_t el_bytes_bit_floor32(uint32_t value);      /* 0 for 0 */
uint32_t el_bytes_bit_ceil32(uint32_t value);       /* 1 for 0; 0 above 2^31 */
/* ...and the same seven with 64 in place of 32. */
```

Decisions in the sketch:

- **Names** follow `el_<module>_<verb>` with the width last, as the loads
  and stores do. The bit-counting names are C23's (`stdc_leading_zeros`
  becomes `el_bytes_leading_zeros32`), so the standard's documentation
  applies to each. Counts return `uint32_t`. C23's return `unsigned int`, but Elemental's ABI rule 2 ([conventions.md](../conventions.md#abi)) asks for a fixed-width type; the two are the same size on every supported target.
- **At 0** every function has the C23 result: no zero is a contract
  violation.
- **Bit ceil without a representable result** (above 2^31, or 2^63)
  **returns 0.** The argument can come from input data, a length read from
  a message for instance, so under
  [conventions.md](../conventions.md#arguments) it cannot be a contract
  violation, and a status would turn a one-line function into an output
  parameter. 0 is never a power of two, so the caller can test for it, and
  it is what glibc 2.44's `stdc_bit_ceil` returns (`<stdbit.h>`, read on
  this host). C23 itself does not promise it, and C++20's `std::bit_ceil`
  makes the case undefined.
- **No 8- and 16-bit counting.** Each derives from the 32-bit function in
  one expression (`el_bytes_leading_zeros32(x) - 16` for a `uint16_t`), and
  no caller needs them yet. Byte swap has all three widths, as decided.
- **No contract violations.** These functions take no pointer.
- **No compiler-runtime helpers.** On ARMv6-M, `__builtin_clz` and
  `__builtin_popcount` become calls to `__clzsi2` and `__popcountsi2`,
  because the core has no CLZ instruction, and ARMv7-M has no population
  count instruction either. A builtin is used only where it is known to
  become an instruction (CLZ where the Arm C Language Extensions define
  `__ARM_FEATURE_CLZ`, and x86-64 and AArch64); everything else is portable
  C. A 64-bit function on a 32-bit core is built from two 32-bit halves, so
  it needs no 64-bit shift or multiply helper. The Zephyr symbol check on
  `qemu_cortex_m0` and `nucleo_g0b1re_renode` enforces this.
- **Byte swap** is written as shifts and masks, which GCC and Clang turn
  into `rev` on Arm and `bswap` on x86-64.

**Tests.** Every 16-bit input exhaustively, as the low and the high bits of
the wider types; each power of two and its neighbours; and a bit-by-bit
reference written in the test, compared on random values, on the host and
in the Zephyr image.

## Amendment (2026-11-13)

**Decision (owner).** Extend `bytes` with the rest of the bit manipulation
that firmware and parsers keep rewriting, in 32- and 64-bit forms only. **No
8- and 16-bit forms, and no C23 `first_leading_one` / `first_trailing_zero`
family** (`first_*`): they derive from the functions below in one expression
and no caller has asked. This is the same reasoning as for the 8- and 16-bit
counts in the amendment above. Still no new module and no new dependency.

```c
/* The rest of C23's counts. */
uint32_t el_bytes_leading_ones32(uint32_t value);   /* 32 for all ones */
uint32_t el_bytes_trailing_ones32(uint32_t value);  /* 32 for all ones */
uint32_t el_bytes_count_zeros32(uint32_t value);

/* Any count; it is taken modulo the width. */
uint32_t el_bytes_rotate_left32(uint32_t value, uint32_t count);
uint32_t el_bytes_rotate_right32(uint32_t value, uint32_t count);
uint32_t el_bytes_reverse_bits32(uint32_t value);

uint32_t el_bytes_mask32(uint32_t width);           /* low `width` bits set */
uint32_t el_bytes_field_get32(uint32_t value, uint32_t shift, uint32_t width);
uint32_t el_bytes_field_set32(uint32_t value, uint32_t shift, uint32_t width,
                              uint32_t field);

uint32_t el_bytes_align_up32(uint32_t value, uint32_t align);   /* 0: no fit */
uint32_t el_bytes_align_down32(uint32_t value, uint32_t align);
bool el_bytes_is_aligned32(uint32_t value, uint32_t align);
/* ...and the same with 64 in place of 32 (`align` is a uint64_t there). */
```

Decisions in the sketch:

- **Rotate counts are `uint32_t` in both widths**, as every count is
  (ABI rule 2), and are taken modulo the width, so no count is a violation:
  a rotate by 0 or by the width is the identity. The opposite shift is
  written `-count & 31`, which is 0 for a count of 0, so no shift is by the
  full width (undefined in C). GCC and Clang turn the pattern into one
  rotate instruction.
- **Full-width masks and fields are defined.** `el_bytes_mask32(32)` is all
  ones. A field of width 0 is valid at any shift up to the type's width and
  returns before it shifts, because that shift may be the full width.
- **A field outside its type is a contract violation**, `width` above the
  type's width or `shift + width` above it, checked as
  `shift <= bits - width` so that a huge `shift` cannot wrap past the
  check. Field positions are layout constants in code: only a program bug
  produces a wrong one. A mask wider than its type is a violation for the
  same reason.
- **Bits of `field` above `width` are ignored, not asserted.** The
  conventions' dividing line is whether a correct program can reach the
  case with bad input ([conventions.md](../conventions.md#arguments)). The
  value being packed can come from input (a length read from a message), so
  an assertion is the wrong tool: with assertions compiled out it would
  turn into silent corruption of the neighbouring field, whereas masking
  costs one AND, gives every input a defined result, and matches what
  assigning to a C bit-field does. A caller that must reject such a value
  tests `el_bytes_bit_width32(field) <= width` first.
- **`align` must be a power of two**, a violation otherwise, 0 included
  (it is a constant of a layout or an ABI, not input). **`el_bytes_align_up`
  returns 0 when the multiple does not fit**, like `bit_ceil`: `value` can be
  a size read from input, so it is not a violation. Unlike `bit_ceil`, 0
  is also the right answer for a `value` of 0, so the test is `result < value`,
  which holds exactly when the multiple did not fit.
- **`align` has the width of `value`** (`uint32_t`, `uint64_t`), not
  `size_t`: these are fixed-width bit functions, and an arena that wants
  `size_t` calls the 32- or 64-bit form for its target. The arenas may adopt
  `el_bytes_align_up` later; this change does not touch them.
- **No compiler-runtime helpers**, as above, and the same method. A 64-bit
  shift by a variable count would call `__aeabi_llsl` on a 32-bit core, so
  the 64-bit mask, field, and rotate functions are built from 32-bit halves
  there; the two helpers that do it replace the previous private
  `power_of_two64`. On x86-64 and AArch64 they are the plain shifts and
  compile to a rotate instruction. The 64-bit `align_up` needs only add, or
  and compare, which a 32-bit core does inline.

**Tests.** A bit-by-bit (or remainder-based) reference for each in the test,
compared on every rotate count from 0 to twice the width, every field
position, every power-of-two alignment, every 16-bit value for bit reversal,
and random values; a death test for each asserted violation, the wrapping
`shift + width` included; and the edges of those contracts, which must not
trip a check. The same bit tests run against the portable code
(`bytes.portable`) and in the Zephyr image.
