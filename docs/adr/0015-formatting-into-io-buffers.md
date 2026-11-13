# 0015. Formatting appends to an io buffer; output is a prefix that ends on an item

Status: Accepted (2026-11-13). Decided by the project owner with the
[io buffers and fmt proposal](../proposals/io-buffers-fmt.md#where-the-truncation-rule-lives)
(decisions 6 to 9 and 15). Supersedes [ADR 0011](0011-formatting-truncation.md).

## Context

ADR 0011 had `fmt` write into its own cursor, `el_fmt`, kept apart from
`io`, because a formatter cannot resume inside a number and a bare
`el_writer` can stall in the middle of one. That cursor turned out to be a
third copy of `io`'s memory writer and grow writer, and it left printf
unable to reach a stream: printing to a UART was a stack buffer, a format
call and `el_write_all`, cut at the stack buffer's size.

M3a gives `io` one buffer type, `el_buf`, and a buffered writer that
borrows one. Both know their free space before anything is written, which
is what "whole or absent" needs. A bare `el_writer` still does not.

## Decision

**printf formats into an `el_buf` or an `el_buffered_writer`; each knows
its free space, so an item enters only if it fits.** `fmt` depends on `io`.
Its core is three pure conversions into a caller's scratch array
(`el_fmt_dec_u64`, `el_fmt_dec_i64`, `el_fmt_hex_u64`), which need neither.

When an item does not fit, the call returns `EL_ERR_NO_SPACE`, or for a
buffered writer the status that kept room from being made
(`el_buffered_writer_reserve`). What was accepted is a prefix of the
unlimited output:

- **An item** is one conversion, or one run of literal text.
- **Numbers are whole or absent.** Sign, padding and digits enter together
  or not at all. Through a buffered writer, passing on may split an
  accepted number across writes; the stream still receives it whole unless
  the stream itself fails.
- **Text is cut at the last byte that fits**: literal text, and `%s` and
  `%c` with their padding.
- **Nothing follows the first item that did not fit, within the call.**
  Nothing carries over to the next call: truncation is not sticky.
- **No terminator is written.** The length is `el_buf_len`.

Into a buffered writer printf streams: when the buffer cannot take the next
item it passes buffered bytes on to make room, so output longer than the
buffer goes out. If the stream stalls, printf stops before the item that
did not fit; resuming means resuming the flush.

## Cost

ADR 0011's "check only the last call" convenience is gone: callers that
compose several calls check each status, or roll back with
`el_buf_truncate`. A buffered stream that stalls mid-line is left
mid-line, unless the caller reserved the line first
(`el_buffered_writer_reserve`). `fmt` now links `io`'s buffer code, about
400 bytes on a Cortex-M0 ([build.md](../build.md#code-size-of-the-assertion-configurations)).

## Revisit when

A consumer needs to resume formatting inside a call, or the length an
unlimited buffer would have taken (as ADR 0011). Or a consumer that composes
many calls keeps getting the chain wrong, which would argue for an opt-in
sticky mode.
