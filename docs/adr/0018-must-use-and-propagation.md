# 0018. A status must be used; passing one on is spelled with macros

Status: Proposed and implemented (2026-11-15), awaiting the project
owner's review. The direction is the owner's
([status handling proposal](../proposals/status-handling.md), decision 1:
start with the syntax); the particulars are the proposal's decisions 3 to
7. Refines [ADR 0003](0003-status-and-progress.md).

## Context

ADR 0003 makes every fallible function return a status. Nothing made a
caller look at it: a call written as a statement compiled silently. And a
caller that did look, only to pass the status on, wrote four lines for it.
Length at every call is what hides the call that has no check.

*Measured* (GCC 16.2.1, Clang 23.1.5): `__attribute__((warn_unused_result))`
works in every language mode of both, and `[[nodiscard]]` in C23 and C++17.
GCC keeps warning through a `(void)` cast for the first and accepts the cast
for the second. GCC rejects `[[nodiscard]]` on a function pointer.

## Decision

- **Every function whose result reports a failure is declared
  `EL_NODISCARD`**: each one that returns `el_status`, and `el_alloc`,
  which returns `NULL`. The macro is `[[nodiscard]]` in C23 and C++17, the
  GNU attribute under GCC and Clang otherwise, and empty elsewhere.
  Operation-table members are not annotated: only the front functions call
  them.
- **A deliberate discard is `el_status_ignore(status)`**, an empty inline
  function. It is the one spelling every compiler accepts, and it takes
  only a status.
- **Passing a status on has four spellings**, all in `elemental/status.h`:
  `EL_TRY(expr)`, `EL_TRY_GOTO(expr, status, label)`, `EL_TRY_NEG(expr)`
  ([ADR 0017](0017-negative-statuses.md)), and `EL_ENSURE(condition,
  status)`, the recoverable counterpart of `EL_ASSERT()`. Each is one
  statement and evaluates its expression once. They are macros because
  each leaves the enclosing function
  ([conventions.md](../conventions.md#abi), rule 6).
- **A status that cannot be anything but `EL_OK` where the call is made is
  asserted**, not ignored: `EL_ASSERT(status == EL_OK)` in module sources.
- A plain `if` stays correct. Code that compares against a particular
  status uses one.
- Not taken from `mresult`: a panic form (`MR_MUST`), result types that
  pack a value with the status, and statement-expression forms. The
  proposal's table gives each reason.

*Checked:* `headers.nodiscard` reads every header for a function returning
`el_status` without the macro; `status.nodiscard_check.*` compiles a
dropped status and expects the diagnostic, once per spelling of the
attribute.

## Cost

A compiler that is neither GCC, Clang, C23, nor C++17 reports nothing, so
the rule is enforced by the toolchains Elemental is verified with and by
no other. A macro that returns hides a way out of a function: a reader has
to know that `EL_TRY` can leave, and a function that holds a resource must
not use it after acquiring one. `EL_TRY_NEG` and `EL_ENSURE` have no `goto`
form. Existing consumer code that drops a status now gets a warning, and an
error under `-Werror`.

## Revisit when

The context decision ([proposal](../proposals/status-handling.md#context))
is made: it may add a parameter or a `_CTX` family. Or a consumer needs the
missing `goto` forms, or a hidden return is found to have leaked a resource
in practice, which would argue for fewer macros, not more.
