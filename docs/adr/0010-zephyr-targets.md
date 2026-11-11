# 0010. Microcontroller targets are Zephyr targets

Status: Accepted (2026-11-11). Decided by the project owner. Supersedes the
freestanding cross build of [ADR 0007](0007-first-slice.md) and the
`cortex-m3` preset kept by [ADR 0008](0008-googletest.md).

## Context

Elemental was checked on microcontrollers by a freestanding cross build: a
Clang toolchain file for a bare Cortex-M3, no C library and no compiler
runtime, compiling the libraries and listing the symbols they leave
undefined. It could not run a test, because the tests are GoogleTest, and
ADR 0008 sent target tests to Zephyr.

With the Zephyr test application working (M1), two routes would check the
same libraries on the same CPUs: a bare toolchain that runs nothing, and
Zephyr, which builds the libraries with a board's real toolchain and runs
the tests. Keeping both doubles toolchain files, presets, and CI jobs for
evidence the second route already gives.

## Decision

**Elemental supports a microcontroller only through Zephyr.** There is no
freestanding cross build, no toolchain file, and no bare-metal preset.

- The libraries stay plain C11 and keep their minimal runtime needs. That
  discipline is still enforced: every Zephyr build for an ARM board checks
  what Elemental's libraries leave undefined, with `tools/check_symbols.py`,
  against an allowlist with reasons
  ([tests/zephyr/allowed-undefined.txt](../../tests/zephyr/allowed-undefined.txt)).
  A new symbol fails the build.
- The symbol check runs for an ARMv7-M board (`mps2/an385`) and an ARMv6-M
  board (`qemu_cortex_m0`), which has no hardware divide and no
  `ldrex`/`strex`.
- The GNU Arm toolchain is the Zephyr SDK's GCC. It builds the Zephyr test
  image, with Elemental's warnings as errors.
- Tests run on `native_sim` and on `mps2/an385` under QEMU
  ([docs/build.md](../build.md#tests-on-zephyr)).

## Cost

- A target Zephyr does not support is not supported. A port to one has to
  bring its own build and its own evidence.
- The symbol check needs a Zephyr workspace (a pinned Zephyr, three west
  modules, the SDK's Arm toolchain), not just an LLVM installation. It no
  longer runs in the ordinary presets.
- The libraries are compiled with Zephyr's flags, which include `-std=c17`
  after Elemental's `-std=c11`. That the sources are C11 is checked by the
  host presets only.
- Clang is no longer used for any Arm build.

## Revisit when

- A user needs a microcontroller target that Zephyr does not support.
- The symbol check misses a dependency that a non-Zephyr toolchain would
  have reported.
