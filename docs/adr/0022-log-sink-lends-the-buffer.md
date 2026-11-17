# 0022. A log sink lends the buffer a record's message is written into

Status: Proposed and implemented (2026-11-17); awaiting the owner's
review. The project owner asked to try it, of three alternatives put to
them after M4 shipped. It reverses decision 3 of the
[log proposal](../proposals/log.md), which put the message on
`el_log_printf`'s stack.

## Context

M4's sink took a finished record: a message as bytes and a length. So
`el_log_printf` had to format the message before any sink saw it, and the
logger keeps no state (it is `const`, shareable across contexts when its
sink is), which left the stack as the only place for it:
`LOG_MESSAGE_MAX` bytes, 128 by default. The writer sink then copied the
message into its own line buffer behind the line's start.

That made two buffers, two copies, and two limits for one printf message.
When the writer sink was given a caller's `el_buf`, fixed or grown, a
grown line buffer still received messages already cut at 128 bytes: no
log call could use any memory but the stack for its message.

The sink is the one object a record's per-call state can live in without
adding a restriction: the contract already makes a sink one context at a
time. A scratch buffer on the logger, decision 3's rejected alternative,
would make a logger one context at a time even over a sink that is not.

## Decision

- **A sink has two operations.** `begin` receives the record without its
  message and lends an `el_buf`, which may already hold bytes of the sink's
  own; `end` receives the record with its message, the bytes appended to
  that buffer, and `fit`, `EL_ERR_NO_SPACE` if the message was cut. Either
  may refuse the record with a status; `end` returns `EL_OK` only for a
  record taken whole.
- **`el_log_printf` formats straight into the lent buffer.** It has no
  buffer of its own; `LOG_MESSAGE_MAX` is removed. The sink's buffer is the
  only limit on a message, and a grown one has none but its allocator.
- **`el_log_write` and `el_log_sink_write` copy the message into the lent
  buffer**, cut at a byte if it does not fit.
- **The writer sink formats the line's start in `begin`**, so the message
  lands after it and is not copied again. **The memory sink lends its
  storage** after the record's fields, so the message is formatted where it
  is kept.
- The record, the logger, and every call site are unchanged.

## Cost

- **Every sink must have a buffer to lend.** One that only passes records
  on, such as the tee example, needs a buffer of its own where it needed
  none. Writing a sink is two operations and a buffer, not one function.
- **A message that does not fit is lost, not logged short.** Under M4 a
  printf message was cut at 128 bytes and then logged whole. Now the cut
  happens in the sink's buffer and the sink is told: the writer sink still
  writes the cut line, the memory sink keeps nothing, and both count the
  record as lost.
- **A sink over a grown buffer allocates during a log call**, so it may be
  used only where its allocator may.
- **`el_log_write`'s path is deeper.** *Measured* (GCC 14.3.0 for the
  Cortex-M0+, `-Os`, frames summed from `-fstack-usage`): from
  `el_log_printf` down to the writer sink's operation, 160 bytes against
  296; from `el_log_write`, 192 against 112, because the message is now
  copied into the sink's buffer in a frame of its own.
- The memory sink object holds an `el_buf` (16 bytes on a 32-bit target)
  and the writer sink the line's start (8 bytes). *Computed* from the
  fields' sizes on a 32-bit target, not measured.

## Revisit when

- Sinks written outside the repository mostly lend a buffer only to copy
  out of it, as the tee does: the record-at-once operation would then be
  worth offering beside this pair.
- A message that does not fit should be logged short rather than counted
  lost.
- M5's deferred sink needs a record's message in a ring it does not own as
  one contiguous buffer.
