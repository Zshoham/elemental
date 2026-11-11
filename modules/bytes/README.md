# bytes

Fixed-width integers in a stated byte order: load and store 16-, 32-, and
64-bit values, little and big endian, at any alignment.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Target | `elemental::bytes` |
| Depends on | `status` (for `EL_ASSERT`). No runtime symbols. |
| Allocates | No |
| Kconfig | `BYTES` |

## What is here

`el_bytes_load_{le,be}{16,32,64}` and `el_bytes_store_{le,be}{16,32,64}`, in
[`elemental/bytes.h`](include/elemental/bytes.h). The design and its review
are in the [proposal](../../docs/proposals/bytes-crc.md).

## Using it

```c
el_bytes_store_le16(out, type);
el_bytes_store_le32(out + 4 + len, crc);
uint64_t time_us = el_bytes_load_be64(msg + 3); /* any alignment */
```

Use these instead of casting a byte pointer to a wider pointer, which is
undefined behavior and faults at an unaligned address on ARMv6-M. They are
real functions, not inline ones, so that bindings have symbols to call;
link-time optimization inlines them where it matters. A null pointer is a
contract violation, checked because address 0 is readable on a Cortex-M
without memory protection.

## Tests

| Test | What it checks | Where |
|---|---|---|
| `bytes.unit.*` | The byte-order vectors at every offset 0 to 7, that a store touches exactly its width, high bits, and random values against a byte-by-byte reference. | Host and Zephyr |
| `bytes.violations.*` | A null pointer stops the program, for every function. | Host only (death tests) |

## Known gaps

No signed variants (cast), no 24- or 48-bit widths, no native-order or
byte-swap functions, and no parsing cursor: none has a consumer.
