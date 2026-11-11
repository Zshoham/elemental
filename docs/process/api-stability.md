# API stability

Elemental is at version 0. **Every interface is experimental**, and nothing
here promises binary compatibility at any level.

## Levels

| Level | What users can rely on |
|---|---|
| **Experimental** | Nothing across releases. It may change or be removed in any release, with a changelog entry and no deprecation period. |
| **Stable** | Source compatibility within a major version: code that compiled and was correct keeps compiling and behaving as documented. Removal goes through deprecation. |
| **Deprecated** | Still works. Named in the changelog with its replacement, and kept for at least one minor release. |

The level belongs to a module, and to a backend separately. A stable
interface can have an experimental backend. It is stated in the module's
README and at the top of each public header.

## What stable covers

Covered: the names, signatures, and documented behavior in a public header,
and the numeric values of status codes.

Not covered, at any level:

- **Binary compatibility.** Recompiling is always required. Struct layouts
  are public for allocation and are not a compatibility surface.
- Behavior the header does not document, including what happens on a
  contract violation.
- Kconfig symbols, CMake internals, and anything under `tests/` or
  `examples/`.

Two things hold even for experimental modules, because breaking them
silently is worse than breaking the build: status code values are never
reused, and a public layout never depends on configuration.

## Versions

`MAJOR.MINOR.PATCH`. While the major version is 0, a minor release may break
anything, and the CMake package accepts only the same minor version. From
1.0, a major version is required to break a stable interface.

## Promotion to stable

A module is proposed for promotion with evidence, not on a schedule. The
baseline, to which a module's proposal may add:

1. **Two implementations** of each interface pass the contract suite, on
   platforms that differ in kind (for example a hosted OS and a
   microcontroller), and at least one of them was written by someone other
   than the interface's author.
2. **Two consumers** exist that were not written to demonstrate the module:
   another Elemental module counts as one.
3. **The contract suite covers every documented promise.** Each "must" in an
   operation table maps to a test, and the suite has been checked against
   deliberately broken implementations.
4. **Each backend states a verification level** of emulator run or better
   for the targets it claims.
5. **It is usable from one other language**, shown at least by a sketch that
   compiles and runs.
6. **No open questions** remain in its proposal, and the interface has not
   changed for one minor release.

This follows the idea, though not the detail, of Zephyr's API lifecycle,
which requires an API to have two implementations on different hardware
before it leaves experimental and to have been available across releases
before it is declared stable.

Promotion is a change to the module's README and headers, reviewed like any
other, with the evidence linked.

## Changing an interface

| Change | Experimental | Stable |
|---|---|---|
| Add a function or a status code | Any release | Minor release |
| Add an operation to a table | Any release | Major release. Implementations initialize tables by name, so an added optional operation is null in existing code, but a table's size is part of the contract between a library and a separately compiled backend. |
| Change a signature or documented behavior | Any release, with a changelog entry | Major release, after deprecation |
| Remove something | Any release, with a changelog entry | Major release, after deprecation |

## What would have to be true for an ABI promise

Not now, and not by default. It would need a concrete consumer that cannot
recompile, a list of the targets and compilers covered, opaque types or
size-versioned structs in place of public layouts, and a compatibility test
that links old objects against a new library. Until someone needs that
enough to pay for it, the answer is to recompile.
