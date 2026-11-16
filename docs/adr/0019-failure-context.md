# 0019. Context for a failure lives in a record the caller owns

Status: Proposed (2026-11-16), **not decided and not implemented**. The
question is the owner's, reopened on 2026-11-15. What follows is the
recommendation of the
[status handling proposal](../proposals/status-handling.md#context), from
its examples and measurements. Shown them on 2026-11-16, the owner chose to
leave the question open and go on to M4, whose `log` proposal says whether
it needs an answer. Would refine [ADR 0003](0003-status-and-progress.md).

## Context

ADR 0003 kept a failure to one integer and named what would reopen that:
callers needing context that has no object to live on. `el_cat` is one, and
the owner's `mresult` prototype is built around the answer: a failure that
says where it began and through which calls it passed.

Three shapes were written against the same three examples and built for the
Cortex-M0+ board: a record the caller owns (A), `mresult`'s error returned
by value (B), and a two-word value of status and origin (C). *Measured*
(Zephyr SDK GCC 14.3.0, `-Os`, `nucleo_g0b1re_renode`; the method and the
full table are in the proposal):

| | `EL_TRY` today | A | B | C |
|---|---|---|---|---|
| Storage | none | 24 bytes, once | 60 bytes per frame | 8 bytes per frame |
| Stack per frame in a chain | 8 | 8 | 72 | 24 |
| Code per site passing a failure on, with context | 10 | 18 | 22 | 14, and no context |
| A report holds | status | status, origin, chain | the same | status, origin |

The AAPCS returns a struct larger than four bytes through memory, so C is
not "a status and one more register" on any Cortex-M: it is a hidden
pointer and two stores on every call. And an operation table returns
`el_status`, so under B and C a failure that begins inside an application's
own reader or writer loses its origin on the way up through `io`.

## Decision

- **`el_status` stays the only way a function reports a failure.** Context
  is beside it, never in it and never instead of it.
- **Context is an `el_error` the caller owns**, declared in
  `elemental/error.h` in the `status` module. A function that wants a trace
  takes an `el_error *`. A null record records nothing.
- **A hop is a pointer to a constant `el_site`** (file, line, a static
  text) that the macro at that place defines. The record holds the status,
  the number of hops, and the first four site pointers, the origin first.
  Its layout is fixed: four is not configurable
  ([ADR 0004](0004-private-configuration.md)).
- **Each propagation macro has a `_CTX` form** that notes the hop and then
  does what the plain form does: `EL_TRY_CTX(error, expr, what)`,
  `EL_TRY_GOTO_CTX`, `EL_TRY_NEG_CTX`, `EL_ENSURE_CTX`. The plain forms are
  unchanged and stay the spelling where there is no record.
- **Whoever handles a failure and carries on resets the record**, with
  `el_error_init()`. A record that is only reported needs nothing.
- **Elemental's own functions take no record.** A trace begins in the
  application: at its call into Elemental, or below Elemental, in an
  operation it implemented. Backends in this repository keep their detail
  on their object ([conventions.md](../conventions.md#errors), rule 6).
- **Rendering belongs to `log`** (M4). The record is read through
  accessors; the function that formats one into an io buffer arrives with
  the writer sink, its first consumer.
- Not taken: an error returned by value, in either size; formatted
  messages (`MR_ERRF`), which need storage in the record and a formatter
  under `status`.

## Cost

One more parameter on every function in a chain that wants a trace. The
record is state: a function that retries after a failure and forgets to
reset it reports the old hops with the new status. Each site with context
is 8 bytes of code and 12 of constant data more than `EL_TRY`, and its text
and the file's name are in the image. The text is static, so a report
cannot say which record or how many bytes. Four hops are kept; a deeper
chain is counted and cut. `status` gains a header that most modules never
include, which bends its admission rule: the function behind it is one
object the linker leaves out of a program that does not use it.

## Revisit when

- Threading the record is what keeps applications from using it. That is
  the argument for a by-value form, and the measurement to repeat.
- A microcontroller target that matters returns two words in registers
  (RISC-V does) and stack is not what it is short of.
- A report needs a value, not only static text.
- M4's writer sink shows that four hops, or a record at all, is the wrong
  size for a log line.
