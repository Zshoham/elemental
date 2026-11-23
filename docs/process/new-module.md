# Adding a module

A module starts as a written proposal and becomes code only after the
proposal's examples have survived review. The order matters: the cheapest
place to find that an interface is awkward is in a usage example, before
anything depends on it.

## The path

1. **Proposal.** Copy [module-proposal-template.md](module-proposal-template.md)
   to `docs/proposals/<module>.md` and fill it in. Open it for review on its
   own, with no implementation.
2. **Review.** Reviewers read it from the three seats in the checklist
   below. The proposal is accepted, sent back, or routed to discovery.
3. **Discovery**, only if the proposal has open questions that examples
   cannot settle. Async and IPC are in this category. Prototype code answers
   the questions and is not merged; the findings are written back into the
   proposal, and decisions that will be reargued get an ADR.
4. **Implementation**, as experimental, following the checklist.
5. **Promotion** to stable, later, on evidence
   ([api-stability.md](api-stability.md)).

A proposal may be rejected for being unnecessary. "An existing module or an
external library already does this" is a good outcome.

## Review: three seats

Every proposal and every implementation is read three times.

**The application author consuming it.**
- Can I use it from the header alone, without reading the source?
- Do the two usage examples read naturally, or do they need casts, scratch
  variables, or boilerplate that the interface could have absorbed?
- What do I have to keep alive, and for how long? Is that stated?
- What does each failure mean to me, and can I tell them apart where I
  need to?

**The backend author implementing it.**
- Is the operation contract a short list of "called with" and "must"?
- What does the front function guarantee, so that I do not have to check it?
- Can I implement it on a platform that lacks a feature? How do I say so?
- Is there a contract suite, and can my backend's fixture express its
  limits without lying?

**The maintainer adding the next module.**
- Does it follow [conventions.md](../conventions.md), or does it document an
  exception and why?
- What does it add to the dependency graph, to the shared status
  vocabulary, and to the Kconfig menu?
- Does every option have a use case? Could the linker do the same job?
- What will be hard to change later?

## Implementation checklist

The smallest complete module to copy from is `modules/status`.

Structure:

- [ ] `modules/<name>/` with `Kconfig`, `CMakeLists.txt`,
      `include/elemental/`, `src/`, `tests/`, `README.md`.
- [ ] `rsource` line in the root `Kconfig`; the name added to
      `ELEMENTAL_MODULE_DIRECTORIES` in the root `CMakeLists.txt`, after its
      dependencies. The Kconfig symbol that enables the module is the
      directory name in upper case (`modules/io` is enabled by `IO`).
- [ ] The library is defined with `elemental_add_module()`. Dependencies are
      named in `DEPENDS` and expressed in Kconfig with `select` or
      `depends on`.
- [ ] A platform backend is its own library under `backends/<backend>/`,
      with its own Kconfig symbol, `<MODULE>_<BACKEND>`, that depends on a
      probed platform symbol (`PLATFORM_<PLATFORM>_<FACILITY>` for one
      facility). `<backend>` is the platform, or the platform and the
      facility ([conventions.md](../conventions.md#naming)); the directory
      name is the target minus `<name>_`.

Contract:

- [ ] Each public header opens with: purpose, stability, ownership and
      lifetime, storage, concurrency and execution context, and what counts
      as a contract violation.
- [ ] Each function documents its statuses and what every output parameter
      holds for each.
- [ ] Each function that returns `el_status`, and any that reports failure
      by `NULL`, is declared `EL_NODISCARD`. Sources pass a status on with
      `EL_TRY` and discard one only through `el_status_ignore()`
      ([conventions.md](../conventions.md#errors), rules 8 and 9).
- [ ] An interface has operation tables with "called with" and "must"
      lists, and a public function in front that enforces both.
- [ ] Sources include `elemental_config.h` first. Public headers do not.
- [ ] No allocation, unless an allocator is a parameter.

Verification:

- [ ] Tests are GoogleTest, defined with `elemental_add_test()`. They cover
      boundaries and every failure path, not only the successful one.
- [ ] An interface has a contract suite: a C fixture struct in
      `contract/include/elemental/<name>_contract.h`, the tests in a
      `.hpp` and `.cpp` beside it, defined with
      `elemental_add_contract_suite()`. It is run against at least two
      implementations that differ in capability. Copy `modules/io/contract`.
- [ ] Backend-specific tests for what only that backend promises.
- [ ] A death test that each documented contract violation stops the
      program.
- [ ] The suite was checked against deliberately broken implementations, and
      what it missed was either added or assigned to backend tests.
- [ ] All presets pass, including `sanitize`, and the Zephyr test
      application passes under Twister. Tests registered with
      `elemental_add_test()` inside `if(COMMAND elemental_add_test)` run in
      both.
- [ ] The module has its own Zephyr test image: its name in
      `ELEMENTAL_TEST_MODULES` and its test-name prefixes in
      `ELEMENTAL_TEST_MODULE_<module>` in `tests/zephyr/CMakeLists.txt`
      (configuring the Zephyr application fails until they are), and a
      scenario `elemental.tests.<module>` for all three boards in
      `tests/zephyr/testcase.yaml`. Its image fits the Cortex-M0+ board
      ([build.md](../build.md#memory-the-test-image-needs)).
- [ ] The Zephyr symbol check passes. It covers every module the build
      enables, so a module is checked automatically; any new external
      symbol goes in `tests/zephyr/allowed-undefined.txt` with a reason.
- [ ] A consumer example and, for an interface, a backend example.

Records:

- [ ] `README.md` from the same headings as an existing module's.
- [ ] Each backend has a `PORT.md` ([porting.md](porting.md)).
- [ ] New status codes follow the admission rule in
      [modules/status/README.md](../../modules/status/README.md).
- [ ] The roadmap and, where a lasting decision was made, an ADR.

Public C headers (`*.h`) are checked for self-containment and C++
compatibility automatically, and every header for a status-returning
function without `EL_NODISCARD`; there is nothing to register. A contract
suite's `.hpp` is C++17 by design and is exercised by the tests that use it.

## Configuration options

An option is added with its use case in the help text. A Kconfig `choice` also
adds one non-default alternative to
[configs/variant_defconfig](../../configs/variant_defconfig), preferably one
that builds on both the host and Zephyr, so the `sanitize` preset and the
Zephyr `variant` scenario test it. `integration.choices` tests every option
of every choice the host's compiler can select, with no change; an option
only a target can select (hidden by a probed fact) needs its own Zephyr
scenario, as `CRC_ZEPHYR` has. Before adding one:

- **Would the linker do it?** A function nobody calls is not linked from a
  static library. An option to leave it out adds a configuration to test
  and saves nothing.
- **Is it a property of the toolchain?** Then it is a probed symbol with no
  prompt, not a choice.
- **Does it need to be seen by the consumer's code?** Then it is an
  exception to [ADR 0004](../adr/0004-private-configuration.md) and needs
  that argument made.
- **Is it policy the application should set per instance?** Then it is a
  parameter, not a build option.
