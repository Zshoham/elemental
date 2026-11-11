# 0009. One capacity status, `EL_ERR_NO_SPACE`

Status: Accepted (2026-11-11). Decided by the project owner.

## Context

M2 (allocation) and M3 (formatting) are the first modules that can fail
because storage ran out: an allocator that is exhausted, a growable writer
that cannot grow, a format buffer that is full. Later, a full ring and a
frame larger than its buffer will fail the same way. The earlier documents
planned `EL_ERR_NO_MEMORY` for allocation and `EL_ERR_INVALID_ARGUMENT` for
bad input. The second name invites returning a status for what
[conventions.md](../conventions.md#arguments) says is an assertion.

The admission rule ([ADR 0003](0003-status-and-progress.md)) is that a code
exists when a caller handles it differently from every other code. A caller
whose arena is exhausted and a caller whose format buffer is full do the
same thing: supply more storage, or give up.

## Decision

- One code, `EL_ERR_NO_SPACE = 4`: the storage available to the operation
  was not enough. It covers heap and arena exhaustion inside a function
  that allocates among other things, a caller buffer that is too small,
  and a container that is full. It replaces the planned `EL_ERR_NO_MEMORY`.
- It is not permanent, unlike `EL_ERR_END_OF_STREAM`. The same call can
  succeed with more storage, or after storage is released.
- Each function that returns it documents what its outputs hold, for
  example that a truncated format holds what fitted.
- Allocation itself still reports exhaustion by returning `NULL`
  ([architecture.md](../architecture.md#62-allocation--proposed)). The
  status is for functions that allocate as part of other work.
- There is no `EL_ERR_INVALID_ARGUMENT`. Malformed *data* will get
  `EL_ERR_INVALID_DATA` when `frame` decode returns it (M6), not before.
- The code is admitted with this decision rather than with its first
  module, because `alloc` and `fmt` are built in parallel and must share
  one value. If neither lands, it is removed before release.

## Cost

A caller cannot tell from the code alone whether it was the heap or a
buffer that ran out; it knows which storage it gave the call, so this is
rarely a loss. Code that wants to log "out of memory" specifically has to
know which call allocated.

## Revisit when

A caller is found that must handle heap exhaustion differently from a
full buffer it supplied, in the same call.
