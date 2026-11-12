# Changelog

Every change to a public interface is listed here, whatever its stability
level. See [docs/process/api-stability.md](docs/process/api-stability.md).

## 0.1.0 — unreleased

First milestone: the project's foundation.

- Licensed under Apache-2.0.
- `configs/assert_application_defconfig` is now `configs/variant_defconfig`,
  which also selects 256-entry CRC tables; `configs/assert_off_defconfig` and
  the `assert-off` preset are new.
- `status`: `EL_ERR_NO_SPACE` (4), the one capacity status
  ([ADR 0009](docs/adr/0009-capacity-status.md)).
- `status` (experimental): `el_status`, four codes, `el_status_name()`,
  `EL_ASSERT()`, and the application assertion handler.
- `io` (experimental): `el_reader` and `el_writer`, `el_read`,
  `el_write`, `el_read_exact`, `el_write_all`, memory streams, and the
  io contract suite.
- `io_posix` (experimental): reader and writer over a file descriptor.
- `alloc` (experimental): `el_allocator`, `el_alloc`, and `el_free`, which
  takes back the layout the allocation was made with
  ([ADR 0012](docs/adr/0012-sized-free.md)); `el_layout`, `el_layout_make`,
  `EL_LAYOUT_OF`, and `el_layout_array` describe an allocation's size and
  alignment as one value, in place of `(size, align)` pairs and of
  `el_alloc_array`, which is removed
  ([ADR 0013](docs/adr/0013-allocation-layout.md)); `el_bump_arena` and
  `el_stack_arena` over caller storage; the allocator contract suite
  `ElAllocContract` and the checking allocator `el_alloc_checker`
  (`elemental/alloc_contract.h`, `.hpp`).
- `alloc_libc` (experimental): `el_libc_allocator()`, over `malloc`,
  `aligned_alloc`, and `free`.
- `io` (experimental): `el_grow_writer` (`elemental/io_alloc.h`), a writer
  that grows through an allocator. Part of `elemental::io` when the Kconfig
  symbol `IO_ALLOC` is on (the default), which makes `elemental::io` link
  `elemental::alloc`. With `IO_ALLOC` off, io builds and links no allocator.
- `fmt` (experimental): `el_fmt`, a cursor over caller memory, and
  `el_fmt_str`, `el_fmt_cstr`, `el_fmt_dec_u64`, `el_fmt_dec_i64`,
  `el_fmt_hex_u64`, `el_fmt_hex_bytes`; a printf subset in
  `elemental/fmt_printf.h` (`el_fmt_printf`, `el_fmt_vprintf`,
  `EL_FMT_PRINTF_CHECK`). Truncation keeps a prefix that ends on an item
  ([ADR 0011](docs/adr/0011-formatting-truncation.md)).
- `fmt`: the private choice `FMT_BUILTIN` / `FMT_LIBC`. `FMT_LIBC` converts
  numbers with the C library's `snprintf`, with identical output, and
  depends on the new probed symbol `PLATFORM_SNPRINTF`. No public header
  changes.
- `bytes` (experimental): `el_bytes_load_{le,be}{16,32,64}` and
  `el_bytes_store_{le,be}{16,32,64}`, at any alignment.
- `crc` (experimental): `el_crc32_update` (CRC-32/ISO-HDLC) and
  `el_crc16_ibm3740_update` (CRC-16/IBM-3740), with `EL_CRC32_INIT` and
  `EL_CRC16_IBM3740_INIT`.
- `bytes`: byte swap, `el_bytes_swap{16,32,64}`, and C23-style bit
  counting for 32 and 64 bits: `el_bytes_leading_zeros`,
  `el_bytes_trailing_zeros`, `el_bytes_count_ones`,
  `el_bytes_has_single_bit`, `el_bytes_bit_width`, `el_bytes_bit_floor`,
  `el_bytes_bit_ceil` (each suffixed `32` or `64`). A bit ceil that does not
  fit is 0.
- `crc`: the private choice `CRC_TABLE` is now `CRC_IMPLEMENTATION`, with
  two more options, `CRC_GCC_BUILTIN` and `CRC_ZEPHYR`, offered where CMake
  probes them (`COMPILER_GCC_CRC_BUILTINS`, `PLATFORM_ZEPHYR_CRC`). Results
  are identical; the option symbols `CRC_TABLE_4BIT` and `CRC_TABLE_8BIT`
  are unchanged.
- `io`: a writer may return `EL_ERR_NO_SPACE`, not permanent, when it could
  not get storage. Only writers that allocate do.
- Tests are GoogleTest, pinned and built from source. The io contract suite
  is the GoogleTest suite `ElIoContract` (`elemental/io_contract.hpp`),
  installed as source; `el_io_contract_run()` and its report callback are
  gone. The fixture struct in `elemental/io_contract.h` is unchanged.
- `el_assert_failed()` no longer documents leaving by `longjmp()`. Nothing
  tests that any more, so it is no longer promised.
- The bare Cortex-M3 test image and its QEMU run were removed. Then the
  `cortex-m3` preset and its toolchain file were removed as well:
  microcontroller targets are Zephyr targets
  ([ADR 0010](docs/adr/0010-zephyr-targets.md)), and the tests and the
  symbol check run in the Zephyr test application (`tests/zephyr`).
- The identifier prefix is `el_` / `EL_`, and generated configuration is
  `EL_CONFIG_`; before release it was `elem_` / `ELEM_` / `ELEM_CONFIG_`.
