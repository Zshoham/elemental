# Porting: adding a backend

A port implements one Elemental interface on one platform facility, or
feeds one module's object from one (a receive interrupt filling a ring). It
can live in this repository, under `modules/<module>/backends/<backend>/`,
or in its own, built against an installed Elemental. The steps are the
same. `<backend>` is the platform for its base facility (`posix`, `zephyr`
for the kernel's clock), or the platform and the facility for one a build
may lack (`zephyr_uart`): the rule is in
[conventions.md](../conventions.md#naming).

## Steps

These steps are for an interface's implementation. A port that feeds a
module's object instead follows the variant under "A producer" below.

1. **Read the interface header.** The operation tables list what your
   function is called with and what it must do. That is the whole contract.
2. **Implement** a state struct, the operations, a `static const` table, and
   a function returning a handle. Do not validate arguments the front
   function already guarantees. Do not allocate, start threads, or change
   process-wide state (signal dispositions, locale, descriptor flags the
   caller owns).
   `EL_ASSERT` is for sources built as part of Elemental. A backend kept
   elsewhere uses its own assertion mechanism for its own preconditions;
   most need none, because the front function has already checked the
   arguments.
   Map the platform's failures onto the interface's statuses and keep the
   platform's detail (an errno) on the object, behind an accessor. A
   backend in this repository returns Elemental's codes only; negative
   statuses are the application's
   ([ADR 0017](../adr/0017-negative-statuses.md)), so a backend an
   application keeps for itself may return its platform's negative errno
   as it is.
3. **Write a contract fixture** and run the shared suite. Leave a capability
   null if your platform cannot be put in that state. A fixture that fakes a
   capability makes the suite's result meaningless.
4. **Write backend-specific tests** for everything your header promises
   beyond the interface: status mapping, what you do not touch, platform
   limits.
5. **Write `PORT.md`** from the template below.
6. **State the verification level honestly.** The levels are defined in
   [build.md](../build.md#verification-levels).

Worked examples: [the POSIX backend](../../modules/io/backends/posix/PORT.md)
in this repository;
[examples/backend_size_classes](../../examples/backend_size_classes),
[examples/backend_step_clock](../../examples/backend_step_clock) and
[examples/backend_log_tee](../../examples/backend_log_tee) for a backend
written the way one outside it would be; and
[examples/consume_installed](../../examples/consume_installed) for running
an installed contract suite from another project.

## A producer: feeding a module's object

Some platform facilities do not implement an interface: they produce into
an object a module already has, and the application reads through that
object's own adapters. A UART's receive interrupt filling a ring is one
([ring_zephyr_uart](../../modules/ring/backends/zephyr_uart/PORT.md)).
Such a port has no operation table, no handle and no wrapper around the
object's adapters: a reader that only forwards to `el_ring_as_reader()`
would reintroduce what the object already is. Steps 1 to 3 become:

1. **Read the object's header**: which of its functions belong to which
   side, and its rules for ownership, set-up, moving and context. That is
   the contract the port joins.
2. **Implement** the producer: the caller provides the object, and the
   port uses only the side it is given, from the moment it is handed over
   until it hands it back. State both moments in the port's header, and
   what a failure leaves of the object (best: nothing touched).
3. **Test its effect on the object**: what it writes, what it counts, what
   it leaves after a failure, and how it hands the object back. Where the
   object's adapter implements an interface, run that interface's contract
   suite over a fixture that joins the port and the adapter (the UART's
   reader is the ring's reader), so the suite checks the composition the
   application uses.

Steps 4 to 6 are the same, and so is the checklist, read with "the
object's contract" for "the interface header".

## Making a backend selectable only where it can build

An in-tree backend gets a Kconfig symbol that depends on a fact about the
toolchain, so that selecting it for the wrong target is a configuration
error and not a compile error. To add such a fact:

1. Probe it in [cmake/ElementalConfiguration.cmake](../../cmake/ElementalConfiguration.cmake),
   next to the existing `<unistd.h>` check, and pass the result in
   `_elemental_kconfig_env` as `EL_PLATFORM_<NAME>=y` or `n`.
2. Declare a symbol with no prompt in the root `Kconfig`:
   `config PLATFORM_<NAME>` with `def_bool $(EL_PLATFORM_<NAME>)`.
3. Make the backend's symbol `depends on` it.

A fact that gates one facility of a platform is named
`PLATFORM_<PLATFORM>_<FACILITY>` (`PLATFORM_ZEPHYR_UART`, `CONFIG_SERIAL`;
`PLATFORM_ZEPHYR_UART_IRQ`, interrupt-driven UARTs too), and each port pins
exactly what it calls. No umbrella probe is invented.

## Two kinds of test

| | Shared contract suite | Backend-specific tests |
|---|---|---|
| Checks | What the **interface** promises, for any implementation. | What **this backend** promises about its platform. |
| Lives | With the interface, as GoogleTest source any project can link. | With the backend. |
| Example | End of stream is permanent. A writer copies what it accepts. | `EPIPE` is reported as end of stream. The descriptor is never closed. |
| A failure means | The backend breaks the interface. | The backend breaks its own documentation. |

Neither replaces the other. If a behavior is in your header and in neither
kind of test, it is not verified.

Both kinds are GoogleTest. To run the shared suite, write a fixture (a C
struct of functions that put your backend into each state the suite asks
for) and instantiate the suite with it, which is one line of C++:

```cpp
#include <elemental/io_contract.hpp>

#include "my_fixture.h"

INSTANTIATE_TEST_SUITE_P(MyBackend, ElIoContract, testing::Values(&my_fixture));
```

Inside this repository, `elemental_add_test()` builds it. Outside, link
`elemental::io_contract` and your own GoogleTest
([example](../../examples/consume_installed/CMakeLists.txt)). The suite
needs a hosted C++17 toolchain, so it runs on a Linux host, or on a Zephyr
target through the Zephyr test application
([build.md](../build.md#tests-on-zephyr)).

## Checklist

- [ ] Operations meet every "must" in the interface header.
- [ ] No allocation, threads, or process-wide side effects.
- [ ] Blocking behavior and thread safety documented in the backend's header.
- [ ] Contract suite passes; skipped tests are explained in `PORT.md`.
- [ ] Backend-specific tests cover the status mapping and the documented
      limits.
- [ ] A Kconfig symbol that depends on a probed platform symbol, so the
      backend cannot be selected where it cannot build.
- [ ] `PORT.md` complete, with the verification level and what was not
      verified.
- [ ] Runtime dependencies listed; for a freestanding port, checked.

## PORT.md template

```markdown
# Port: <interface> on <platform>

| | |
|---|---|
| Interface | |
| Platform facility | |
| Target | `elemental::<name>` |
| Header | |
| Stability | Experimental |
| Verification level | compile check / symbol check / link check / emulator run / tested on a host / hardware test, and on what |

## Capabilities

| Interface behavior | Supported | Notes |
|---|---|---|

## Unsupported semantics

<!-- What a caller might expect and will not get. What the port
     deliberately does not do. -->

## Toolchain and runtime dependencies

<!-- Compiler and version tested. Every symbol and library needed. -->

## Configuration

<!-- Kconfig symbols and the platform symbol they depend on. -->

## Concurrency and context

<!-- Thread safety, interrupt handlers, when it blocks. -->

## Verification

| Test | Kind | What it checks |
|---|---|---|

<!-- Then: what it was run on, and what was not verified. -->
```
