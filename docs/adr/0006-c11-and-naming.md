# 0006. C11 baseline and the `el_` prefix

Status: Accepted (2026-11-11)

## Context

The language baseline decides which toolchains can build Elemental and what
the interfaces can express. The prefix is permanent once anyone links
against it.

## Decision

**C11, without compiler extensions**, for sources and public headers. Public
headers are also valid C++11.

- C99 lacks what resource-explicit interfaces need: `_Alignof` and
  `max_align_t` for allocation, `_Static_assert` for layout checks,
  `_Noreturn`.
- C11 is available in every toolchain in the intended range: GCC and Clang
  for a decade, and the vendor compilers built on them.
- C17 is C11 with corrections and needs no separate statement.
- C23 would give fixed underlying types for enums and `[[nodiscard]]`, both
  directly useful here, but it is not yet a safe assumption for vendor
  toolchains.

**The prefix is `el_`**, with `EL_` for macros and constants,
`EL_CONFIG_` for generated configuration, `<elemental/...>` for headers,
and `elemental::` for CMake targets. The project owner chose `el_` on
2026-11-11 because it reads better, knowing that libedit exports an `el_`
family. The previous choice was `elem_`, `ELEM_`, `ELEM_CONFIG_`.
`elm_` belongs to EFL's Elementary.

## Cost

No C99-only toolchains. Without C23, status codes are an anonymous enum
plus a typedef rather than a typed enum, and unused results are not flagged
by the compiler.

The prefix is shared with libedit. Its `histedit.h` declares `el_*`
functions (`el_init`, `el_set`, `el_gets`, ...) and `EL_*` macros, and its
library exports the functions. A program that links both libraries, or
includes `histedit.h` next to Elemental headers, gets a duplicate symbol or
a conflicting declaration if an Elemental name ever equals a libedit one.
None does today: checked on 2026-11-11 against the symbols exported by
libedit 3.1 (20260512) and the `el_`/`EL_` names in its `histedit.h`. New
public names must be checked against libedit the same way.

## Revisit when

- A target in the intended range has only a C99 compiler. The public headers
  are close to C99 already; the question would be whether to promise it.
- C23 is available across the toolchains users actually have. Typed enums
  and `[[nodiscard]]` would then replace conventions with compiler checks.
- An Elemental name collides with a libedit name, or with another library
  Elemental's users link.
