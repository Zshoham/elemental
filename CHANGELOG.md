# Changelog

Every change to a public interface is listed here, whatever its stability
level. See [docs/process/api-stability.md](docs/process/api-stability.md).

## 0.1.0 — unreleased

First milestone: the project's foundation.

- Licensed under Apache-2.0.
- `status`: `EL_ERR_NO_SPACE` (4), the one capacity status
  ([ADR 0009](docs/adr/0009-capacity-status.md)).
- `status` (experimental): `el_status`, four codes, `el_status_name()`,
  `EL_ASSERT()`, and the application assertion handler.
- `io` (experimental): `el_reader` and `el_writer`, `el_read`,
  `el_write`, `el_read_exact`, `el_write_all`, memory streams, and the
  io contract suite.
- `io_posix` (experimental): reader and writer over a file descriptor.
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
