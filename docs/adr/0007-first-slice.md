# 0007. The first slice is status and byte I/O

Status: Accepted (2026-11-11). Its freestanding cross build is superseded by
[ADR 0010](0010-zephyr-targets.md).

## Context

The first implementation exists to test the architecture, not to be useful
on its own. It is limited to two small modules. The status vocabulary was
one; the other was a choice between allocation and byte I/O.

## Decision

Status and byte I/O.

The deciding factor was which choice puts the most pressure on the decisions
every later module inherits:

- **The error convention needs a demanding client.** A stream has four
  outcomes per call (progress, end, would-block, failure) and partial
  progress on top. Allocation has one failure mode and would have left the
  status vocabulary essentially unused, validated by nothing.
- **The contract suite is stronger.** Three implementations differ in ways
  that matter: memory streams end but never stall, a ring pipe stalls but
  does not end after a fixed count, a pipe descriptor does either depending
  on a flag the caller owns. That forced the suite to separate universal
  promises from optional capabilities.
- **The native backend carries real evidence.** A POSIX descriptor has
  partial writes, `EINTR`, `EPIPE`, and a caller-owned blocking mode. A
  `malloc` wrapper would have shown little.
- **It demonstrates a module that needs no allocator**, which is a stated
  goal, rather than starting from the module that is one.

Both candidates exercise the handle model, caller-owned storage, the
freestanding build, and interop equally well.

## Cost

The allocation interface, which several later modules will take as a
parameter, is still only a proposal, and its sized-free question is
untested. I/O is also the module most likely to be revisited when async is
designed.

## Revisit when

Not applicable to the choice itself. Its consequence is that allocation is
the next milestone, and that the io contract must be re-read against the
findings of the async discovery milestone.
