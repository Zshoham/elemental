# crc

Two cyclic redundancy checks, named as in the
[CRC catalogue](https://reveng.sourceforge.io/crc-catalogue/):
CRC-32/ISO-HDLC and CRC-16/IBM-3740 (also called CRC-16/CCITT-FALSE).

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Target | `elemental::crc` |
| Depends on | `status` (for `EL_ASSERT`). No runtime symbols, except Zephyr's CRC functions under `CRC_ZEPHYR`. |
| Allocates | No |
| Kconfig | `CRC`; the choice `CRC_IMPLEMENTATION`: `CRC_TABLE_4BIT` (default), `CRC_TABLE_8BIT`, `CRC_GCC_BUILTIN`, `CRC_ZEPHYR` |

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

| Option | Computes with | Available | Size, Cortex-M3 |
|---|---|---|---|
| `CRC_TABLE_4BIT` (default) | 16-entry tables, 96 bytes; 2 lookups per byte | Always | SIZE_4BIT bytes |
| `CRC_TABLE_8BIT` | 256-entry tables, 1.5 KiB; 1 lookup per byte | Always | SIZE_8BIT bytes |
| `CRC_GCC_BUILTIN` | GCC's `__builtin_rev_crc32_data8`, `__builtin_crc16_data8` | GCC 15 or later (probed) | Not measured: the Zephyr SDK's GCC 14.3.0 lacks the builtins. SIZE_BUILTIN_HOST bytes on x86-64 (GCC 16.2.1, `-Os`) |
| `CRC_ZEPHYR` | Zephyr's `crc32_ieee_update()`, `crc16_itu_t()` | Zephyr, `CONFIG_CRC=y`, no `CONFIG_CRC_HW_HANDLER` (probed) | SIZE_ZEPHYR_EL bytes, plus SIZE_ZEPHYR_FN bytes of Zephyr's two functions |

Results are identical with every option; nothing a caller sees changes.
Sizes are text, including the tables, with assertions trapping in place,
built for `mps2/an385` by the Zephyr SDK's GCC 14.3.0 at Zephyr's default
size optimization ([build.md](../../docs/build.md#code-size-of-the-assertion-configurations)).
The speed difference between the tables is Pigweed's measurement, not one
made here.

`CRC_GCC_BUILTIN` exists for a GCC that lowers the builtins to a CRC
instruction. GCC 16 on x86-64 does not, even with `-mpclmul`: it generates
a 1.5 KB table. `CRC_ZEPHYR` saves the code twice over in a Zephyr
application that already uses Zephyr's CRCs. Zephyr's hardware handler
(`CONFIG_CRC_HW_HANDLER`) replaces those functions with driver calls that
return 0 on failure, so the option is not offered with it. A CRC engine
behind Zephyr's driver API is a different, fallible interface, deferred in
the [module plan](../../docs/module-plan.md#deferred-a-crc-engine). The
decisions are in the proposal's
[amendment](../../docs/proposals/bytes-crc.md#amendment-2026-11-12).

## Tests

| Test | What it checks | Where |
|---|---|---|
| `crc.unit.*` | Run with the 16-entry tables by the default presets, with the 256-entry ones by `sanitize` ([configs/variant_defconfig](../../configs/variant_defconfig)), with every option the host's compiler can select by `integration.choices`, and with `CRC_ZEPHYR` by the Zephyr scenario `elemental.tests.crc_zephyr`. The catalogue's check values; the CRC of no bytes; lengths 0 to 300 against a table generated with Python's `zlib.crc32` and `binascii.crc_hqx`; every split point; the CRC-16 residue; unaligned input. | Host and Zephyr |
| `crc.violations.*` | Null data with a nonzero length stops the program. | Host only (death tests) |

## Known gaps

No other CRCs, and no hardware CRC units: a CRC engine needs a device and
can fail, so it is a separate interface, deferred until a consumer needs it.
Until then `el_crc32_update` is a test oracle for one.
