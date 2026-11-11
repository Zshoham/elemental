# Proposal: <module name>

Status: Draft | Accepted | In discovery | Rejected
Author, date:

<!-- Keep it short. A proposal that needs more than a few pages is probably
     two modules. Delete these comments as you go. -->

## Problem

<!-- Who has this problem, and what do they do today? One paragraph. -->

## Usage examples

<!-- Two realistic examples, as complete C, written before the header. One
     should be the common case, one an awkward case: failure handling,
     constrained storage, or two instances at once. -->

### Example 1: <the common case>

```c
```

### Example 2: <the awkward case>

```c
```

## Backend example

<!-- For an interface: one implementation of the operations, as it would be
     written by someone outside this repository. If nothing varies by
     platform, say that there is no backend and why. -->

```c
```

## Why not something that exists

<!-- An existing Elemental module, libc, or an external library. Be
     specific about what is insufficient. -->

## Dependencies and cost

| | |
|---|---|
| Elemental modules | |
| Runtime symbols (freestanding) | |
| Allocation | none / takes an allocator / explain |
| Per-object storage | |
| New status codes | |

## Contracts

- **Ownership and lifetime:** who creates, who ends, what is borrowed and
  for how long.
- **Errors:** each status, what it means to the caller, what the outputs
  hold.
- **Concurrency:** thread safety, reentrancy.
- **Execution context:** interrupt handlers, blocking.
- **Contract violations:** what is asserted.
- **Exceptions to docs/conventions.md,** with reasons.

## Interface model

<!-- Which of the mechanisms in ADR 0002, and why not a weaker one. What is
     decided at build time and what per instance. Whether several instances
     and backends coexist. -->

## Configuration

| Option | Default | Use case | Why not the linker / a parameter |
|---|---|---|---|

## Verification

- **Contract tests:** the promises the suite will check, and the optional
  capabilities.
- **Implementations to run it against:**
- **Backend-specific tests:**
- **Portability matrix:**

| Target | Toolchain | Level intended (compile / symbol / link / emulator / hardware) |
|---|---|---|

## Evidence required before stable

<!-- What would have to be true. See api-stability.md for the baseline; add
     what is specific to this module. -->

## Open questions

<!-- If any of these cannot be settled by examples, the proposal goes to
     discovery. Say what a prototype would have to show. -->
