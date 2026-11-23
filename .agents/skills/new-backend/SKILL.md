---
name: new-backend
description: Port an Elemental interface to a platform - the backend, its contract fixture, its own tests, and PORT.md. Use when adding or changing an implementation of an interface, under modules/*/backends/ or kept outside this repository.
---

# New backend

The process, the checklist, and the `PORT.md` template are
`docs/process/porting.md`. The contract is the interface header: its
"called with" and "must" lists are everything the backend has to meet.

A port that feeds a module's object instead of implementing an interface
(a receive interrupt filling a caller's ring: `ring_zephyr_uart`) has no
operation table, handle or wrapper of the object's adapters. Follow "A
producer" in `docs/process/porting.md` for steps 1 to 3; its contract is the
object's header, and the contract suite runs over a fixture that joins the
port and the object's adapter.

## Copy from

| For | Look at |
|---|---|
| The smallest complete backend, with its fixture and both kinds of test | An `examples/backend_*/` directory |
| A backend in this repository, with a Kconfig symbol and a `PORT.md` | An existing `modules/<module>/backends/<backend>/` |
| A backend built against an installed Elemental, bringing its own GoogleTest | `examples/consume_installed/` |

## Steps

1. **Implement** a state struct, the operations, a `static const` table,
   and a function returning a handle. The front function has already
   checked the arguments. The backend leaves alone what the caller owns,
   and process-wide state.

2. **Write the fixture in C** and instantiate the interface's contract
   suite (`modules/<module>/contract/`), one line of C++:

   ```cpp
   INSTANTIATE_TEST_SUITE_P(MyBackend, <ContractSuite>, testing::Values(&my_fixture));
   ```

   A capability the platform cannot be put into stays null, and the tests
   that need it report as skipped. Done when the suite passes and every
   skipped test has its reason in `PORT.md`.

3. **Write the backend's own tests** for what its header promises beyond
   the interface: status mapping, what it does not touch, platform limits.
   Done when each such promise has been seen red (`writing-tests`).

4. **In-tree only: make it selectable only where it builds.** Its Kconfig
   symbol, `<MODULE>_<BACKEND>`, `depends on` a probed platform symbol,
   `PLATFORM_<PLATFORM>_<FACILITY>` when it gates one facility; adding a
   probe is the three steps under "Making a backend selectable only where
   it can build" in `docs/process/porting.md`. `<backend>` is the platform
   (`posix`), or the platform and the facility (`zephyr_uart`): the rule is
   under "Naming" in `docs/conventions.md`. Define the library with
   `elemental_add_module(<module>_<backend> ... DEPENDS <module>)` in
   `modules/<module>/backends/<backend>/`, and add it from the module's
   `CMakeLists.txt` inside `if(EL_CONFIG_<SYMBOL>)`.

5. **Write `PORT.md`** from the template, with the verification level
   reached and on what, and a list of what was not verified. The levels a
   backend can honestly claim are the ones `docs/build.md`, "Verification
   levels", marks as done today.

6. **Finish.** Done when every box of the checklist in
   `docs/process/porting.md` is ticked or reported as unmet, the new skip
   counts are in the module README's contract-suite table, and `preflight`
   passes.
