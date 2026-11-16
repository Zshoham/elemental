# 0021. The log cut-off is a macro the consumer defines

Status: Accepted (2026-11-16) with the [log proposal](../proposals/log.md)
(decision 1), which the project owner accepted as a whole; the particulars
here are the proposal's recommendation, following
[module-plan.md](../module-plan.md#3-decisions-that-must-come-first), and
were not decided one by one. The first exception to
[ADR 0004](0004-private-configuration.md), which named logging as the
module that would need one.

## Context

A log call below some level should cost nothing in a release build: no
call, no format string in the image. Only the caller's translation unit
can remove its own call, so the level has to be visible there, and ADR
0004 says no public macro's expansion depends on configuration. It asks a
module in this position to choose between a compile definition carried by
its CMake target and a public generated header, and to make a mismatch
between caller and library a build or link error.

Both of those make the level part of Elemental's configuration, and so
something the library and its caller can disagree about. The owner's
`mlog` prototype does neither: `MLOG_STATIC_LEVEL` is a macro the user
defines, and the library never sees it.

*Measured* (GCC 16.2.1 and Clang 23.1.5, x86-64, the proposal's Example
1): with the call inside `if ((level) >= EL_LOG_CUTOFF)`, a call below the
cut-off and its format string are absent from the object at `-O0` and at
`-Os`.

## Decision

- **`EL_LOG_CUTOFF` is a macro the consumer may define** for a target or
  a file, before `elemental/log_printf.h` is included, to one of the
  `EL_LOG_LEVEL_*` values. Undefined, it is `EL_LOG_LEVEL_DEBUG`: every
  call is kept. It works as `NDEBUG` does for `<assert.h>`.
- **Elemental's configuration never sets it.** There is no Kconfig symbol
  and no compile definition on `elemental::log`. The library has run-time
  levels per logger and no compile-time level at all.
- **So there is nothing to mismatch**, and no guard. Two files of one
  program may use different cut-offs; each removes its own calls.
- **A removed call is still compiled.** Its format is checked against its
  arguments either way. Its arguments are not evaluated, so they must
  have no side effect the program depends on.
- **Only the `EL_LOG_*` macros honor it.** `el_log_write` and
  `el_log_printf` are functions, and the contract other languages use.
- This is an exception to ADR 0004 in the letter (a public macro whose
  expansion varies) and not in what the rule protects: no type, layout,
  or signature changes, and the thing that varies is the consumer's own.
  `tests/headers` still compiles the header with no configuration.

## Cost

The level is not in `elemental_config.h`, so one build's cut-off is found
in its compile flags, not in Elemental's `.config`. A consumer that wants
one cut-off everywhere has to define it everywhere; a file that misses it
keeps its debug calls, silently. Removal depends on the compiler dropping
a branch whose condition is constant: both compilers measured do so
without optimization, and a compiler that did not would still not run the
call. A call's arguments are evaluated or not depending on a build flag.

## Revisit when

- A consumer needs the library itself to drop code by level (a sink's
  level names, say). That would be a private Kconfig option, not this.
- A compiler in the verified set keeps a cut call's format string.
- A second public macro wants to vary with something the consumer
  defines. The test is this record's: does any type, layout, or signature
  change, and can caller and library disagree?
