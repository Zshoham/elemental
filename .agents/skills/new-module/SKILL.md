---
name: new-module
description: Add a module or interface to Elemental, from written proposal to experimental implementation. Use when asked to propose, design, or implement a new module, or to add an interface to an existing one.
---

# New module

The process is `docs/process/new-module.md`; the rules a public header
follows are `docs/conventions.md`. Read both in full before writing a
header. This skill is the order of work and the places that are easy to
miss.

## Steps

1. **Place it.** `docs/roadmap.md` is the order and each milestone's
   acceptance. `docs/module-plan.md` is what each module is, its
   dependencies, and the decisions it waits on. Done when every module the
   new one depends on exists, and every decision it waits on is recorded,
   or the report says which is not.

2. **Proposal first.** Copy `docs/process/module-proposal-template.md` to
   `docs/proposals/<module>.md`. Write the two usage examples and the
   backend example as complete C before designing the header: the examples
   are the design review. Done when every section of the template is
   filled, the examples need no casts or scratch variables the interface
   could have absorbed, and each open question is listed. Then hand the
   proposal back for review **on its own**. Implementation starts once the
   owner has accepted it.

   A proposal whose open questions examples cannot settle goes to
   discovery: prototype code answers the questions and stays out of the
   tree.

3. **Implement as experimental.** Work through the "Implementation
   checklist" in `docs/process/new-module.md`. Copy the layout of the
   smallest existing module under `modules/`; for an interface, one that
   has a `contract/` directory. Tests follow the `writing-tests` skill; a
   backend follows `new-backend`.

4. **Register it.** A module is wired in at each of these:

   | Where | What |
   |---|---|
   | Root `Kconfig` | `rsource "modules/<name>/Kconfig"` |
   | Root `CMakeLists.txt` | `<name>` in `ELEMENTAL_MODULE_DIRECTORIES`, after its dependencies |
   | `modules/<name>/Kconfig` | The enabling symbol is the directory name in upper case; dependencies as `select` or `depends on`, matching `DEPENDS` in `elemental_add_module()` |
   | `README.md` | A row in the module table |
   | `docs/build.md` | "The options" table, for each new symbol |
   | `CHANGELOG.md`, `docs/roadmap.md` | The new interface; the milestone |

   Public `*.h` headers are picked up by the header checks, and a
   module by the Zephyr symbol check, with nothing to register. Tests
   registered with `elemental_add_test()` also run in the Zephyr test image.

5. **Finish.** Done when every box in the implementation checklist is
   ticked or reported as unmet with its reason, and `preflight` passes.

## Decisions that need an argument

- **A Kconfig option** needs a use case in its help text and has to survive
  the four questions under "Configuration options" in
  `docs/process/new-module.md`. A function nobody calls is already left out
  by the linker.
- **A status code** follows the admission rule in
  `modules/status/README.md`. Values are never reused.
- **A public name** follows the naming table in `docs/conventions.md` and
  differs from every name libedit's `histedit.h` declares.
- **Allocation** happens only through an allocator passed as a parameter.
- **A lasting design decision** gets a record in `docs/adr/`.
