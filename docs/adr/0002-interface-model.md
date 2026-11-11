# 0002. Concrete types first; handles where backends coexist; link-time only for singletons

Status: Accepted (2026-11-11)

## Context

There are three ways to let an implementation vary in C: a function the
linker resolves, a context pointer with a table of functions, and operations
passed in by the caller. Each has a real cost. A project that picks one and
applies it everywhere pays that cost where it buys nothing.

## Decision

Use the weakest mechanism that does the job:

1. **Plain functions on caller-owned objects** by default. No indirection.
2. **A handle** (`void *ctx` plus `const <interface>_ops *`, passed by
   value) when implementations must coexist in one program and the caller
   must not know which it has. The public function in front of the table
   validates the caller's arguments and checks the implementation's result.
3. **A function the application defines**, resolved at link time, only for a
   hook that is one-per-program by nature and has no instance to dispatch
   on. Today: `el_assert_failed()`.
4. **Operations passed for one call** where no object needs to persist.

Before any of these, prefer logic that takes its inputs as arguments and
calls nothing.

Configuration selects which backends exist in a build. It never selects the
backend of an interface; the application chooses per instance.

There is no universal object model, and modules are not required to look
alike.

## Cost

An indirect call per handle operation, and no inlining across it. A lifetime
rule for handles that the compiler does not check. Less uniformity between
modules.

## Revisit when

- A handle's indirect call is measured to matter. The alternative considered
  is embedding the interface struct in the implementation, which allows a
  non-virtual fast path at the cost of `container_of` arithmetic that is
  awkward from Rust and Python.
- A second link-time hook is proposed. The test is unchanged: is there
  really no instance that could carry the choice?
- A backend must supply a type or a macro to its callers, which neither a
  handle nor the linker can do. That is the case for a compile-time facade,
  and there is none yet.
