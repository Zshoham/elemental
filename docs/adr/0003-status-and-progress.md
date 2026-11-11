# 0003. Integer status codes; progress or status, never both

Status: Accepted (2026-11-11)

## Context

Every module inherits the error convention, so it is expensive to change.
The familiar C conventions each have a known failure: `errno` is hidden
global state; "count or negative error" overloads one value and invites
sign bugs; "zero bytes means end of file" is ambiguous with a zero-length
request; returning a count together with an error leaves callers unsure
which to act on.

## Decision

- A recoverable failure is an `el_status`: `int32_t`, `EL_OK == 0`,
  positive `EL_ERR_*` codes from one small shared vocabulary.
- A fallible function returns the status and delivers results through output
  parameters, which are always written.
- **A single-attempt operation makes progress or reports a status, never
  both.** `EL_OK` means at least one unit moved; anything else means none
  did. A failure found after partial progress is reported by the next call.
- End of stream and would-block are statuses. A count of zero carries no
  meaning.
- Operations that loop over a single-attempt operation report the status
  and the progress made, and are documented as resumable.
- A code enters the vocabulary when a caller would handle it differently
  from every existing code and a module in the repository returns it.
  Values are never reused.
- Platform detail (an `errno`) stays with the object that failed.
- Programmer mistakes are assertions, not statuses.

## Cost

An output parameter and a status check on every call, where a returned count
would be shorter. A failure after partial progress is seen one call late.
Codes are coarse: a caller cannot distinguish kinds of I/O failure without
asking the backend. `el_status` is a plain integer, so the compiler does
not stop it being mixed up with other integers.

## Revisit when

- Callers repeatedly need context that has no object to live on. That
  argues for an optional diagnostics module wrapping a status, not for a
  larger status.
- A real backend cannot report a late failure on the next call, for example
  because there is no next call.
- Integer mix-ups show up as actual bugs. The alternative is a one-member
  struct, which costs ergonomics and, on 32-bit x86, a register return.
