# 0011. Formatting writes to caller memory; truncation keeps a prefix that ends on an item

Status: Accepted (2026-11-11). Decided by the project owner.

## Context

`fmt` (M3) is the first module that writes text into a buffer that may be
too small, and `log` (M4) builds on it. `ring` and `frame` will face the same
question with bytes. [ADR 0009](0009-capacity-status.md) fixed the status,
`EL_ERR_NO_SPACE`, and left each function to say what its output holds.
`snprintf` cuts at any byte, so a number can be cut mid-digit and read as a
different number, and later output can follow a part that was dropped.

Formatting into an `el_writer` instead was rejected: a writer can return
`EL_ERR_WOULD_BLOCK` halfway through a number, and a formatter cannot resume
inside a digit sequence ([module-plan.md](../module-plan.md#named-by-the-owner)).

## Decision

Text formatting writes to caller memory through `el_fmt`, its own cursor.
It does not depend on `io`. The caller sends the result with
`el_write_all`, which can be resumed.

When the storage is not enough, the call returns `EL_ERR_NO_SPACE`, and
the buffer holds a prefix of what unlimited storage would hold:

- **An item** is one call of the core API, one printf conversion, or one
  run of literal text in a printf format.
- **Numbers are whole or absent.** Sign, padding, and digits go in together
  or not at all.
- **Text is cut at the last byte that fits.** This covers strings, literal
  text, and `%s` and `%c` with their padding. A cut may split a UTF-8
  sequence.
- **A hex dump is cut after the last whole byte.**
- **Truncation is sticky.** After the first item that does not fit whole,
  nothing more is written, and every call returns `EL_ERR_NO_SPACE` until
  the cursor is initialised again. So checking the last call's status is
  enough.
- **The buffer is NUL-terminated** after every call when its capacity is
  above zero. The length never counts the terminator.

## Cost

One byte of every buffer goes to a terminator that `el_write_all` and Rust
do not need. A truncated message may lose more than strictly had to be lost:
a number that would have fitted partly is left out entirely, and short text
after it is not written. Callers who want a trailing newline even on
truncation must hold a byte back themselves.

## Revisit when

A consumer needs to resume formatting after truncation, or needs the length
an unlimited buffer would have taken (the `snprintf` return value). Neither
exists today.
