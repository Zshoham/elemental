# bytes

Fixed-width integers in a stated byte order: load and store 16-, 32-, and
64-bit values, little and big endian, at any alignment. Also byte swap, and
the bit counting of C23 `<stdbit.h>` for C11.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Target | `elemental::bytes` |
| Depends on | `status` (for `EL_ASSERT`). No runtime symbols, and no compiler helpers such as `__clzsi2`. |
| Allocates | No |
| Kconfig | `BYTES` |

## What is here

In [`elemental/bytes.h`](include/elemental/bytes.h):

- `el_bytes_load_{le,be}{16,32,64}` and `el_bytes_store_{le,be}{16,32,64}`;
- `el_bytes_swap{16,32,64}`;
- for 32 and 64 bits, C23's bit counting: `el_bytes_leading_zeros`,
  `el_bytes_trailing_zeros`, `el_bytes_count_ones`, `el_bytes_has_single_bit`,
  `el_bytes_bit_width`, `el_bytes_bit_floor`, and `el_bytes_bit_ceil`, each
  with the width as a suffix (`el_bytes_leading_zeros32`).

The design and its review are in the
[proposal](../../docs/proposals/bytes-crc.md); byte swap and bit counting in
its [amendment](../../docs/proposals/bytes-crc.md#amendment-2026-11-12).

## Using it

```c
el_bytes_store_le16(out, type);
el_bytes_store_le32(out + 4 + len, crc);
uint64_t time_us = el_bytes_load_be64(msg + 3); /* any alignment */

uint32_t slots = el_bytes_bit_ceil32(requested);  /* 0 if above 2^31 */
```

Use these instead of casting a byte pointer to a wider pointer, which is
undefined behavior and faults at an unaligned address on ARMv6-M. They are
real functions, not inline ones, so that bindings have symbols to call;
link-time optimization inlines them where it matters. A null pointer is a
contract violation, checked because address 0 is readable on a Cortex-M
without memory protection.

Bit counting is defined for every input, as in C23: 0 has 32 (or 64)
leading and trailing zeros, a bit width of 0, a bit floor of 0, and a bit
ceil of 1. A bit ceil that does not fit the type, for a value above 2^31
(or 2^63), is 0. A compiler builtin is used only where it becomes an
instruction, so ARMv6-M, which has no CLZ, gets portable C rather than a
call to `__clzsi2`.

## Tests

| Test | What it checks | Where |
|---|---|---|
| `bytes.unit.*` | The byte-order vectors at every offset 0 to 7, that a store touches exactly its width, high bits, and random values against a byte-by-byte reference. | Host and Zephyr |
| `bytes.unit.BytesSwap.*`, `bytes.unit.BytesBits.*` | Byte swap and bit counting against bit-by-bit references: every 16-bit value, every 8-bit pattern at every bit position, each power of two and its neighbours, 0, and random values of every width. | Host and Zephyr |
| `bytes.portable.*` | The same bit-counting tests, with `bytes.c` compiled with its builtins turned off, so the portable code ARMv6-M uses also runs on the host. | Host only |
| `bytes.violations.*` | A null pointer stops the program, for every function. | Host only (death tests) |

## Known gaps

No signed variants (cast), no 24- or 48-bit widths, no native-order
functions, no 8- or 16-bit bit counting (subtract from the 32-bit result),
and no parsing cursor: none has a consumer.
