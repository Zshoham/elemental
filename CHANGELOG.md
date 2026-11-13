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
- `bytes`: the rest of C23's counts, `el_bytes_leading_ones`,
  `el_bytes_trailing_ones`, `el_bytes_count_zeros`; `el_bytes_rotate_left`,
  `el_bytes_rotate_right` (any count, taken modulo the width), and
  `el_bytes_reverse_bits`; bit fields, `el_bytes_mask`,
  `el_bytes_field_get`, `el_bytes_field_set` (a field wider than its width is
  ignored; a field outside its type is a contract violation); and
  `el_bytes_align_up`, `el_bytes_align_down`, `el_bytes_is_aligned`, for a
  power-of-two alignment (anything else is a contract violation; `align_up`
  is 0 when the multiple does not fit). Each is suffixed `32` or `64`.
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
- `io` (M3a): `el_buf` (`elemental/io_buf.h`), a memory buffer with fixed
  storage: `el_buf_init`, `el_buf_data`, `el_buf_data_mut`, `el_buf_len`,
  `el_buf_room`, `el_buf_append` (whole or nothing), `el_buf_reserve`,
  `el_buf_spare`, `el_buf_commit`, `el_buf_truncate`, and the memory
  endpoint `el_buf_as_writer`, which reports `EL_ERR_NO_SPACE` when full.
  `el_grow_buf` (`elemental/io_alloc.h`, `IO_ALLOC`): `el_grow_buf_init`,
  which returns its `el_buf *`, and `el_grow_buf_deinit`. They replace
  `el_mem_writer` (removed from `elemental/io_mem.h`) and `el_grow_writer`
  (removed). A full memory writer now reports `EL_ERR_NO_SPACE`, not
  `EL_ERR_END_OF_STREAM`; `EL_ERR_NO_SPACE` is no longer only from writers
  that allocate.
- `io_contract` (M3a): the optional fixture capability `writer_full_after`
  and the test `WriterNoSpaceKeepsWhatFitAndRepeats`. A fixture written with
  designated initializers before it existed leaves it null and skips it.
- `io` (M3a): `el_buffered_writer` (`elemental/io_buffered.h`), a buffer in
  front of another writer: `el_buffered_writer_init`, `_pending`, `_flush`,
  `_reserve`, `_as_writer`. Flush is a function of the type, not an
  operation of `el_writer` ([ADR 0014](docs/adr/0014-flush-on-the-concrete-type.md),
  provisional). `io` now needs `memmove` as well as `memcpy`.
- `fmt` (M3a): reworked on `io` ([ADR 0015](docs/adr/0015-formatting-into-io-buffers.md),
  superseding ADR 0011). `elemental/fmt.h` is three pure conversions,
  `el_fmt_dec_u64`, `el_fmt_dec_i64`, `el_fmt_hex_u64(out, cap, value[,
  flags])`, returning the length, with `EL_FMT_INT_LEN_MAX` (20) and
  `EL_FMT_UPPER` (now 1). `el_fmt_printf` and `el_fmt_vprintf` take an
  `el_buf *`; `el_fmt_printf_buffered` and `el_fmt_vprintf_buffered` are
  new, into an `el_buffered_writer`. Removed: `el_fmt`, `el_fmt_init`,
  `el_fmt_len`, `el_fmt_str`, `el_fmt_cstr`, `el_fmt_hex_bytes`,
  `EL_FMT_ZERO_PAD`, the width and flags of the conversions, NUL
  termination, and sticky truncation (truncation is per call). `fmt` now
  depends on `io` (Kconfig `select IO`). The private choice `FMT_BUILTIN` /
  `FMT_LIBC` is removed.
- The identifier prefix is `el_` / `EL_`, and generated configuration is
  `EL_CONFIG_`; before release it was `elem_` / `ELEM_` / `ELEM_CONFIG_`.
