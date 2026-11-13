# 0016. A private choice may widen what a function accepts, never change it

Status: Accepted (2026-11-13). Decided by the project owner with the
[io buffers and fmt proposal](../proposals/io-buffers-fmt.md#the-platform-option-on-printf)
(decision 10). Refines [ADR 0004](0004-private-configuration.md).

## Context

ADR 0004 keeps configuration out of public headers: signatures, layouts and
macros are the same in every configuration, and configuration changes only
which libraries exist and how they behave. Until M3a every choice option
kept behavior identical too: `CRC_*` and the old `FMT_BUILTIN` /
`FMT_LIBC` produced the same output.

`FMT_PRINTF_LIBC` is the first option that changes **what a function
accepts**. Under it `el_fmt_printf` takes `%+d`, `%.2f` and `%p`, through
the C library's `snprintf`; under the default, `FMT_PRINTF_BUILTIN`, the
same format is a contract violation. A format correct in one image is a bug
in another, while every header is identical.

## Decision

A private choice may widen the set of inputs a function accepts, under five
rules:

1. **The default accepts the least.**
2. **For every input that every option accepts, the output is identical**,
   and tested identical in each configuration. For printf this holds by
   construction: the portable subset is the same built-in code under both
   options, and never reaches `snprintf`.
3. **Inputs outside the default's set are contract violations under it**,
   asserted where cheap.
4. **The public header documents each set and names the option**
   (`elemental/fmt_printf.h`).
5. **Code that must build against any configuration** (`log`, a library
   over `fmt`) **uses only the common set.**

No option may change the result of an input the default accepts, or reject
an input the default accepts.

## Cost

A program that works in one image can stop at an assertion in another,
built from the same sources. Only the common set is portable, so a library
author must know which set they are writing to; the header says so, and
the default configuration's tests catch a violation where assertions are
on. With assertions compiled out, a violation stops formatting at that
specification, and nothing reports it.

## Revisit when

A second option wants to widen a contract, and these rules do not decide
it; or a consumer is caught by the difference in practice, which would argue
for a run-time query or a compile-time guard on the wider set.
