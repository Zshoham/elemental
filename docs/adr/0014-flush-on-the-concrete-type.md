# 0014. Flush is a function of the buffered writer, not an operation of `el_writer`

Status: Accepted (2026-11-13). Decided by the project owner with the
[io buffers and fmt proposal](../proposals/io-buffers-fmt.md#flush-and-el_writer_ops)
(decision 5), provisionally until M4's writer sink existed. **Confirmed by
that sink on 2026-11-16**: `el_log_writer_sink` builds each line in its own
storage and passes it on with one `el_write_all`, so it holds nothing
between records and has nothing to flush
([log](../../modules/log/README.md#choosing-a-sink)). M6's `frame` is the
next to test it ([Revisit when](#revisit-when)).

## Context

M3a adds the first writer that holds bytes back: `el_buffered_writer`
(`elemental/io_buffered.h`). Its bytes reach the inner stream only when it
is flushed, or when a later write needs room. The roadmap asks for the
decision about flush to be recorded with the log example that decided it
([M7](../roadmap.md#m7--io-second-iteration)). The log module does not
exist yet, so the decision is taken on the planned log design and stays
open to M4's evidence.

The test, from [module-plan.md](../module-plan.md): flush becomes an
operation only if a holder of a bare `el_writer` handle must flush.

*Source facts* (the proposal's Sources): Rust makes `flush` a required
method of `Write`, so every writer implements or forwards it. Go keeps
`Flush` on `*bufio.Writer`, off `io.Writer`. Pigweed's `pw_stream::Writer`
has no flush.

## Decision

Flush is `el_buffered_writer_flush()`, a function of the concrete type.
`el_writer_ops` keeps its one operation, `write`.

- The planned log writer sink is handed an `el_writer`, formats each record
  into its own `el_buf`, and writes it with one `el_write_all`. It has
  nothing to flush.
- Where a buffered writer is used, the code that created it owns it and
  flushes it through the concrete type.

If a writer that wraps another and is itself handed out as a bare handle
needs it (a `frame` encoder over a buffered UART, M6), flush becomes an
optional operation at the end of `el_writer_ops`: a null operation means
"nothing to flush" and returns `EL_OK`.

## Cost

Code that holds only an `el_writer` cannot flush what a buffered writer
behind it holds. Whoever hands out a buffered writer's handle must also
arrange for it to be flushed. Adding the operation later is a breaking
change to `el_writer_ops` (conventions, ABI rule 4), with the cost of the
operation table, a front function, a contract-suite capability and
forwarding in every wrapper.

## Revisit when

M4's writer sink is built: it confirms this record or reopens it. Or `frame`
(M6), or any wrapper handed out as a bare `el_writer`, needs to flush
through the handle.
