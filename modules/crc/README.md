# crc

Two cyclic redundancy checks, named as in the
[CRC catalogue](https://reveng.sourceforge.io/crc-catalogue/):
CRC-32/ISO-HDLC and CRC-16/IBM-3740 (also called CRC-16/CCITT-FALSE).

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Target | `elemental::crc` |
| Depends on | `status` (for `EL_ASSERT`). No runtime symbols. |
| Allocates | No |
| Kconfig | `CRC`; `CRC_TABLE_4BIT` (default) or `CRC_TABLE_8BIT` |

## What is here

`el_crc32_update`, `el_crc16_ibm3740_update`, and their start values
`EL_CRC32_INIT` and `EL_CRC16_IBM3740_INIT`, in
[`elemental/crc.h`](include/elemental/crc.h). The design, the choice of
CRC-16, and the review are in the [proposal](../../docs/proposals/bytes-crc.md).

## Using it

```c
uint32_t crc = el_crc32_update(EL_CRC32_INIT, data, len);   /* one shot */

uint16_t crc16 = EL_CRC16_IBM3740_INIT;                     /* in pieces */
crc16 = el_crc16_ibm3740_update(crc16, chunk, n);
```

The start value is the CRC of no bytes and every result is final, so a
result is passed straight back to continue. A message followed by its
CRC-16 stored big endian has a CRC-16 of 0.

## Configuration

| Choice | Tables | Lookups per byte | Size, Cortex-M3 |
|---|---|---|---|
| `CRC_TABLE_4BIT` (default) | 16 entries each, 96 bytes | 2 | 224 bytes |
| `CRC_TABLE_8BIT` | 256 entries each, 1.5 KiB | 1 | 1 624 bytes |

Results are identical; nothing a caller sees changes. Sizes are text,
including the tables, with assertions trapping in place, built for
`mps2/an385` by the Zephyr SDK's GCC 14.3.0 at Zephyr's default size
optimization ([build.md](../../docs/build.md#code-size-of-the-assertion-configurations)).
The speed difference is Pigweed's measurement, not one made here.

## Tests

| Test | What it checks | Where |
|---|---|---|
| `crc.unit.*` | Run with the 16-entry tables by the default presets and with the 256-entry ones by `sanitize` ([configs/variant_defconfig](../../configs/variant_defconfig)). The catalogue's check values; the CRC of no bytes; lengths 0 to 300 against a table generated with Python's `zlib.crc32` and `binascii.crc_hqx`; every split point; the CRC-16 residue; unaligned input. | Host and Zephyr |
| `crc.violations.*` | Null data with a nonzero length stops the program. | Host only (death tests) |

## Known gaps

No other CRCs, and no hardware CRC units: those are the application's
optimisation, and `el_crc32_update` is a test oracle for one.
