# 0023. Atomics are loads and stores of one word; an interrupt handler is one more context

Status: Accepted (2026-11-17). Decided by the project owner, with the
[ring proposal](../proposals/ring.md), over the alternative of a
critical-section operation supplied to every ring. Implemented in
[`ring`](../../modules/ring/README.md) and the deferred log sink. It
follows the recommendation in
[module-plan.md](../module-plan.md#3-decisions-that-must-come-first),
except for the static assertion that recommendation asks for, which cannot
be written.

## Context

`ring` is the first object used from two contexts at once: an interrupt
handler or a thread writes, another thread reads.
[conventions.md](../conventions.md#concurrency-and-execution-context) says
no module contains a lock or an atomic, and that one that needs one takes
it as an explicit decision.

*Measured* (Zephyr SDK 1.0.1's GCC 14.3.0, `-Os`, 2026-11-17):

- For Cortex-M0 and Cortex-M0+ (ARMv6-M), `atomic_load_explicit` and
  `atomic_store_explicit` of an `_Atomic size_t` compile to one `ldr` or
  `str` and a `dmb`: no call, the same code as for a Cortex-M3.
- `atomic_fetch_add` on the same targets is a call to
  `__atomic_fetch_add_4`, which neither the compiler nor Zephyr provides.
- `ATOMIC_INT_LOCK_FREE`, `ATOMIC_LONG_LOCK_FREE` and
  `ATOMIC_POINTER_LOCK_FREE` are 1, "sometimes", on ARMv6-M, and 2 on
  ARMv7-M. A static assertion that the word is lock-free would reject the
  Cortex-M0, where the load and the store are inline.
- Clang 23.1.5 for `thumbv6m-none-eabi` calls `__atomic_load_4` and
  `__atomic_store_4` for the same two operations, unless the target
  feature `atomics-32` is given. No ARM target here is built with Clang.

*Source facts* (Zephyr v4.5.0, read locally): its own `ring_buf` keeps
indices that only one side writes, loads the other side's with acquire and
stores its own with release, through compiler builtins, and only in a
multi-CPU build; on one CPU it uses plain loads and stores
(`include/zephyr/sys/ring_buffer.h`). On ARMv6-M the kernel implements its
`atomic_*` operations in C under an interrupt lock
(`ATOMIC_OPERATIONS_C`, `arch/arm/core/cortex_m/Kconfig`).

`<stdatomic.h>` is not a C++11 header and `_Atomic` is not a C++ keyword;
every public header is valid C++11
([conventions.md](../conventions.md#language-baseline)).

## Decision

- **The only atomic operations in Elemental are a load and a store of an
  `_Atomic size_t`**, from C11's `<stdatomic.h>`: `atomic_load_explicit`
  with acquire, `atomic_store_explicit` with release, and `atomic_init`. A
  context may read with relaxed a word that only it writes.
- **No read-modify-write**: no `fetch_add`, exchange, or compare-exchange,
  no atomic flag, no type wider than a word. No lock, no critical section,
  and no `volatile` in place of an atomic.
- **Each shared word has one context that writes it.** That is enough for
  one producer and one consumer. A count that one context keeps is a load
  and a store.
- **A module that needs more takes a critical-section operation from its
  caller**, as a parameter, and uses no global one. None does.
- **Checked by the symbol check, not by an assertion**: no `__atomic_*`
  symbol in Elemental's libraries on any ARM board, the Cortex-M0 among
  them. A probed Kconfig symbol says whether the toolchain compiles the
  two operations; a module that uses them depends on it.
- **A public struct declares the word as `_Atomic size_t` in C and as
  `size_t` in C++.** The source asserts that both have one size and
  alignment. The header does not include `<stdatomic.h>`, and C++ code
  does not touch the field: a struct's fields are not API.
- **An interrupt handler is a context like any other.** "ISR-safe" keeps
  its meaning in conventions.md: does not block, does not allocate, takes
  no lock, and is safe under the context rule the header states. For
  `ring` that rule is one context per side, at a time. Two handlers that
  can preempt each other are two contexts.

## Cost

- **A `dmb` with each acquire load and release store on Arm**, two per
  read or write, on a single-core microcontroller that does not need
  them. Elemental cannot see whether a build has one CPU, as Zephyr's
  ring does.
- **Several producers need the application's exclusion.** A ring written
  by a thread and by a handler needs interrupts masked around the
  thread's writes, by the application.
- **Clang for ARMv6-M is not supported** without `atomics-32`. The symbol
  check would report it.
- **C++ sees a different type** for the word than C does, with the same
  layout by assertion, not by the language.
- **Nothing here shows memory ordering on a multi-core Arm.** The host
  test runs on x86-64, which orders more strongly; see the proposal's
  record of what the evidence shows.

## Revisit when

- A second module needs a read-modify-write.
- A target that matters cannot load or store a word without a call.
- The barrier is measured as a cost in a handler.
- A consumer needs several producers on one ring often enough that the
  application's exclusion is most of what it writes.
