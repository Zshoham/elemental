# 0017. Negative status values belong to the application

Status: Accepted (2026-11-15). That negative values are the application's
was decided by the project owner with the
[status handling proposal](../proposals/status-handling.md) (decision 2);
what follows from it here (the pass-through rule, in-tree backends,
`EL_TRY_NEG`) is the proposal's. Refines
[ADR 0003](0003-status-and-progress.md).

## Context

ADR 0003 made `el_status` an `int32_t` with `EL_OK == 0` and positive
`EL_ERR_*` codes, admitted one at a time. It said nothing about the other
values, so an application that wrote its own `el_status` functions on top
of Elemental had no code to return for a failure of its own. The consumer
example returned `EL_ERR_IO` for a record too long for its length byte.

The platforms Elemental runs on already have a convention for the
application's side of this. *Source fact:* Zephyr and the Linux kernel
report a failure as a negative errno value, with zero or a positive value
for success. The owner's `mresult` prototype treats every nonzero code as
the user's and lifts such returns with `MR_TRY_NEG`.

## Decision

- **Elemental defines no negative status and never returns one of its
  own.** Every code it adds is positive.
- **Negative values are the application's**, to number as it likes. Where
  the platform reports negative errno values, such a value is an
  application status as it stands: no table, no conversion.
- **Elemental's functions return a negative status unchanged** when an
  operation the application implemented behind an interface returns one.
  It follows the rules of any other status: for a single-attempt operation,
  no progress.
- **Backends that ship in Elemental do not use them.** They return an
  Elemental code and keep the platform's detail on their object
  ([conventions.md](../conventions.md#errors), rule 6).
- `el_status_name()` gives `"unknown"` for a negative value. The
  application names its own.
- `EL_TRY_NEG(expr)` passes on a negative result of a call that returns a
  count or a negative errno. A call that returns only zero or a negative
  errno goes to `EL_TRY` directly.

## Cost

One number space now holds three vocabularies: Elemental's, the
application's, and the platform's errno where the application uses it. An
application that mixes its own negative codes with negative errno values
has to keep them apart itself. The integer mix-ups ADR 0003 accepted
become a little more likely: a POSIX call's `-1` given to `EL_TRY_NEG`
travels as a status, though it is not the failure (`errno` is). Lowering an
Elemental code to an errno value at a platform boundary still needs a
mapping, which `status` cannot hold because it may not include `<errno.h>`.

## Revisit when

An application needs positive codes of its own (a protocol that defines
them, say), or an interface needs to tell an application status from an
Elemental one in order to act on it. Or the mix-ups above show up as bugs,
which is ADR 0003's own condition for a struct in place of the integer.
