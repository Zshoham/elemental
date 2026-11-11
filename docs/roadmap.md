# Roadmap

Small milestones, each with a way to tell it is done. Nothing listed after
M0 exists yet.

This document is the order. What each module is, what it depends on, what
was rejected, and the decisions each one waits on are in
[module-plan.md](module-plan.md).

| | Milestone | Depends on | Kind |
|---|---|---|---|
| M0 | Foundation | — | **Done** |
| M1 | Close the verification gaps | M0 | Evidence |
| M2 | Allocation | M0 | Module |
| M3 | Formatting, bytes, crc | M0 | Modules |
| M4 | Time and logging | M3 | Modules |
| M5 | Ring and interrupt context | M4 | Module and decision |
| M6 | Framing | M3 (`bytes`) | Module |
| M7 | I/O, second iteration | M4, M6 | Module revision |
| M8 | IPC discovery | M6 | Discovery |
| M9 | Async discovery | M2, M5, M7 | Discovery |
| M10 | io promotion; async and IPC implementation | M8, M9 | Decided by discovery |

**Parallel.** M1, M2, and M3 can run together; every "on a Zephyr target"
item waits for M1's target tests. M6 can start as soon as `bytes` lands,
alongside M4 and M5. M8 and M9 can run together.

**Why this order.** M3, M4, and M5 each settle a contract decision that
everything after them inherits: formatting into caller memory, the time
representation and the first exception to
[ADR 0004](adr/0004-private-configuration.md), and atomics and interrupt
context. Each is settled on the smallest module that forces it. Discovery
comes last because it learns most when time, rings, framing, and real
interrupt evidence already exist, and io is not promoted until async
discovery has reported ([ADR 0007](adr/0007-first-slice.md)).

Allocation is off that path: it depends only on `status`, and nothing from
M3 to M8 takes an allocator. It still comes first, because:

- It is the second interface and the second contract suite. The handle
  model of [ADR 0002](adr/0002-interface-model.md), and contract suites
  with capabilities, have so far been tried only on streams. `time` and
  `log` will follow whatever pattern exists by then.
- It settles the capacity status code and sized free before other modules
  are built against them.
- Applications want it, and as a leaf nothing can block it.

The cost: until async or IPC discovery, its only consumers are `io_alloc`
and its own examples. Its design is checked by examples, not by a module
that depends on it, and pools may revise it. Done one module at a time, it
puts formatting and logging one module later.

## M0 — Foundation (done)

The architecture, the build, and one slice that exercises both.

Delivered: `status`, `io`, `io_posix`, the io contract suite, an example
backend, two consumer projects, the Kconfig and CMake integration, GoogleTest
as the test framework, a freestanding cross build checked at compile and
symbol level, and the process for what follows. What was run is in
[build.md](build.md#what-is-verified).

## M1 — Close the verification gaps

M0 was verified on one machine, and only on a host: no test runs on a
microcontroller target since the tests moved to GoogleTest
([ADR 0008](adr/0008-googletest.md)). Before more is built on it, close
that gap and find out what one machine was hiding. This should not wait.

Deliverables and acceptance:

- [ ] **Target tests under Zephyr.** The unit tests and the io contract
      suite (memory streams and the ring pipe) build as a Zephyr
      application with GoogleTest and are run by Twister, first on
      `native_sim`, then on one emulated Cortex-M board. Recorded: the
      Zephyr revision, how GoogleTest is supplied, the memory the test
      image needs, and the evidence level reached. If GoogleTest does not
      fit a Cortex-M target, that is the finding, and ADR 0008's revisit
      condition applies.
- [ ] **A Cortex-M0 (ARMv6-M) build** with the symbol check. It has no
      `ldrex`/`strex` and no hardware divide, so it shows whether M4 and M5
      pull in `__aeabi_uldivmod` or `__atomic_*`. Running the tests on a
      Cortex-M0 Zephyr board is a bonus, not a requirement; GoogleTest may
      not fit one.
- [ ] **The CI workflow passes** on its first real run, or is fixed until it
      does. It has never run.
- [x] **A license is chosen** and added: Apache-2.0, in `LICENSE`.
- [ ] **Older compilers.** The CI image's GCC and Clang build with warnings
      as errors, or the warning set is adjusted with reasons.
- [ ] **A second operating system** for the POSIX backend (macOS or a BSD):
      contract suite and platform tests pass, `PORT.md` updated.
- [ ] **A GNU Arm toolchain** job alongside the Clang one, with its own
      symbol check. Its compiler runtime needs will differ.
- [ ] **One hardware run**: the same Zephyr test application on a real
      Cortex-M board, recorded with the board named.
- [ ] **Python 3.9** runs the configuration step, since that is the stated
      minimum.

Out of scope: new interfaces.

## M2 — Allocation

[Sketch](architecture.md#62-allocation--proposed);
[plan entry](module-plan.md#named-by-the-owner).

Deliverables:

- An ADR on the capacity and bad-input statuses, before the proposal:
  [ADR 0009](adr/0009-capacity-status.md), `EL_ERR_NO_SPACE`. `io_alloc` is the first module to return a capacity
  status.
- A proposal, with usage examples that put sized free under pressure: a
  string, a growable buffer, an object handed across an API. The examples
  are mandatory, because no module in this roadmap takes an allocator
  before discovery. The proposal says what async and IPC discovery may
  later ask of pools.
- `el_allocator`, an arena over caller storage, a libc heap backend, and a
  contract suite with capabilities (reclaims on free; bounded).
- `io_alloc`: a growable `el_writer` in a separate library, as the first
  consumer and as the test that `io` does not acquire a dependency on
  allocation.

Acceptance:

- [ ] The contract suite passes for the arena, the libc backend, and one
      example allocator, and was checked against broken implementations.
- [ ] The cross symbol check shows no new external symbols.
- [ ] Once M1 has target tests: the arena's tests pass on a Zephyr
      Cortex-M target.
- [ ] `elemental::io` still links without `elemental::alloc`.
- [ ] The sized-free decision is recorded in an ADR with the examples that
      decided it.
- [ ] A Rust sketch shows how the allocator contract relates to
      `GlobalAlloc`, compiled and run.

Deferred within the module: resize in place, pools, statistics, thread
safety.

## M3 — Formatting, bytes, crc

All pure computation.

- [ ] Proposals for `fmt`, `bytes`, and `crc`. Each is short; `bytes` and
      `crc` can share one review.
- [ ] `fmt` output is byte-identical on the host and on a Zephyr Cortex-M
      target. Truncation returns the capacity status from M2, and the
      output holds what fitted.
- [ ] The printf-subset header is checked by the compiler's format
      attribute where one is available. Its core is callable from the Rust
      sketch.
- [ ] `bytes` and `crc` are tested against published vectors on the host
      and on a Zephyr Cortex-M target.
- [ ] No external symbols beyond `memcpy` on Cortex-M3 and Cortex-M0.

## M4 — Time and logging

- [ ] An ADR fixing the time representation, before the log record is
      designed.
- [ ] A `time` contract suite run against the manual clock and the POSIX
      backend.
- [ ] Two sinks coexist in one program: memory and a POSIX descriptor
      through the writer sink.
- [ ] The log sink contract suite exists, defined with
      `elemental_add_contract_suite()` like `io_contract`.
- [ ] The compile-time cut-off removes call sites, shown by a size or
      symbol check. Its ADR is recorded as the first exception to
      ADR 0004.
- [ ] No clock is read except through the handle the caller passed.
- [ ] The author's earlier `mlog` prototype is reviewed. The proposal lists
      what was taken and what was left.

Not in this milestone: logging from an interrupt handler (M5).

## M5 — Ring and interrupt context

- [ ] An ADR on atomics and interrupt context
      ([module-plan.md](module-plan.md#3-decisions-that-must-come-first)).
- [ ] `ring` passes the io contract suite through its adapters. The ring
      pipe example is retired or rebased onto it.
- [ ] On a Zephyr Cortex-M target under QEMU, an interrupt-driven UART
      example fills a ring from its interrupt handler. A task reads it as
      an `el_reader`. The deferred log sink is used from an interrupt
      handler on the same image.
- [ ] A stress test on the host: a producer thread and a consumer thread,
      under ThreadSanitizer.
- [ ] No `__atomic_*` symbol on Cortex-M0.
- [ ] Recorded honestly: the emulator shows ordering on one core, not
      timing, and not multi-core memory ordering.

## M6 — Framing

A normal module, not discovery: a Sans-I/O codec has no open question that
examples cannot settle.

- [ ] A proposal with examples of COBS over a lossy stream (resynchronise
      after corrupted bytes) and of length-prefix with a message larger than
      the maximum.
- [ ] Tested as plain function calls, including every split of the input
      across calls.
- [ ] The glue runs over the ring pipe, over a Unix socket via `io_posix`,
      and over the M5 UART example on a Zephyr target with a `crc` trailer.
- [ ] `EL_ERR_INVALID_DATA` is admitted under the
      [status rule](../modules/status/README.md#why-this-module-is-allowed-to-be-everyones-dependency).

## M7 — I/O, second iteration

Additions to `io` that M4 and M6 will have shown to be needed, not guessed:
a buffered writer with flush, and a buffered reader with peek.

- [ ] Each addition names its consumer: `log` or `frame`.
- [ ] The contract suite grows with it, and existing backends still pass.
- [ ] The decision on flush in the operation table is recorded, with the
      log example that decided it.
- [ ] No promotion assessment yet; that waits for M9.

## M8 — IPC discovery

Produces answers, not headers. It starts from the merged `frame` module.

- List, for three real transports, what could not be expressed portably.
- Ask one question: is there a portable core beyond `frame`? The candidate
  is Sans-I/O request and response correlation with deadlines passed in.

Acceptance:

- [ ] A findings document with the portable/transport-specific table
      revised from experience.
- [ ] A decision: `frame` only; `frame` plus an IPC module with a stated
      portable core; or nothing more.

## M9 — Async discovery

No public header comes out of this milestone. It produces answers.

The questions are listed in
[architecture.md](architecture.md#65-async-and-ipc--discovery-before-design).
The work:

- Prototype two models against the same small workload, an echo server
  with a timeout: readiness with a caller-run poll loop, and completion with
  caller-owned operation objects. The workload uses `frame` for messages
  and `time` for its timeout.
- On two backends that differ in kind: Linux (`epoll` or `io_uring`) and
  the M5 interrupt-driven UART example on a Zephyr target under QEMU.
- Review the earlier `myio` prototype for what its seven backends taught.
- Write the Rust and Python sketches for each model before choosing, since
  buffer lifetime is where they will hurt.

Acceptance:

- [ ] A findings document that answers each question or says why it cannot
      be answered yet.
- [ ] Measured, per model: storage per in-flight operation, and what
      happens at the limit.
- [ ] A statement of whether `io` needs to change, so that M10 can assess
      promotion.
- [ ] An ADR choosing a model, or deciding that Elemental provides only the
      non-blocking `io` contract and leaves event loops to applications.
      "Not in scope" is an acceptable result.

## M10 — io promotion; async and IPC implementation

- [ ] `io` assessed against the
      [promotion criteria](process/api-stability.md#promotion-to-stable),
      after the async ADR. Then promoted, or given a list of what is
      missing.
- [ ] Scope for async and IPC set by M8 and M9. Each resulting module goes
      through the normal proposal process. No estimate is offered before
      discovery reports.

## Deferred, deliberately

These are not scheduled. Each needs a reason to exist before it gets a
milestone. Modules considered and rejected for now, with what would bring
each back, are listed in
[module-plan.md](module-plan.md#considered-and-rejected).

- A C runtime, startup code, or libc compatibility
  ([ADR 0001](adr/0001-scope.md)).
- Language bindings. The interop sketches stay sketches.
- A Windows backend, and MSVC as a verified compiler.
- Shared libraries and any binary compatibility promise.
- Distribution through package managers.
- A universal async runtime.

## How this document changes

A milestone is edited when its acceptance criteria are met or found to be
wrong. A new module enters through a proposal, and gets a milestone when the
proposal is accepted.
