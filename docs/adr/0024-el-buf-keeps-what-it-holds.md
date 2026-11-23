# 0024. An `el_buf` keeps what it holds

Status: Accepted (2026-11-23). Decided by the project owner with the
[seams proposal](../proposals/seams.md#what-is-not-changed-and-why), after
a prototype that was built and kept outside main.

## Context

`el_fmt_printf` formats into an `el_buf`, and `el_fmt_printf_buffered`
formats into a buffered writer: two function families for one formatter.
The API review of 2026-11-18 asked whether they could be one target.

A prototype made a buffered writer lend an `el_buf` that passes its bytes
on to the writer when it needs room (branch
`t3/unify-buffer-format-target`, outside main; the numbers are the
delegated agent's that built it). It worked: the gcc, clang and assert-off
presets passed, and sanitize with leak detection off; its io and fmt code
was 40 bytes larger on a Cortex-M0 (Zephyr SDK GCC 14.3.0, `-Os`, object
text).

It also made every `el_buf *` possibly a stream:

- An append that failed on a stalled stream had already passed earlier
  bytes on: appending `abcdefgh` left `cdefgh` in the buffer.
- Offsets into a buffer went stale when it emptied itself, so code that
  patches a length prefix or truncates back to a mark was wrong.
- `log` needed a run-time check to refuse such a buffer, because a sink's
  lent buffer must hold the message it is given.

## Decision

- **An `el_buf` keeps every byte appended to it** until its owner truncates
  it. Growing may move its storage; nothing passes its bytes on.
- **`el_fmt_printf` and `el_fmt_printf_buffered` stay two families.**

## Cost

- Two formatting entry points, and every formatter that wants to reach
  both a buffer and a stream is written against one and adapted to the
  other.

## Revisit when

- A second reusable formatter needs to reach both a buffer and a stream.
  Then the shared target is a handle of its own (`ctx` and `ops`, made
  from an `el_buf` or from a buffered writer), not a mode of `el_buf`.
