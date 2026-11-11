# 0005. Kconfig through a pinned fork, provisioned explicitly

Status: Accepted (2026-11-11)

## Context

Configuration uses Kconfig, through a specific Kconfiglib fork that adds a
CMake package, CMake-format output, and absolute dependency file lists. The build must be reproducible
and must work offline once set up. Two hazards are specific to this
dependency: the fork reports the same version number as upstream
Kconfiglib, and upstream is commonly installed system-wide, where the same
Python would import it instead.

## Decision

- The fork's commit is pinned in one file, `cmake/KconfiglibPin.cmake`.
- A one-time script, `tools/provision.cmake`, clones it into `.deps/`.
  Configure and build never access the network.
- Elemental uses the fork's `kconfig_configure()` as documented, from the
  source checkout. It adds no generation logic of its own.
- Every configure verifies the checkout's commit, that it is unmodified, and
  that Python imports `kconfiglib` from it.
- One addition: `tools/kconfig_check.py`, which uses Kconfiglib's evaluator
  to turn selections Kconfig would silently drop into errors.

Alternatives not taken: `FetchContent` (network access during configure,
once per build directory); a git submodule (ties consumers to a submodule
workflow and to the fork's URL scheme); requiring a `pip`-installed
Kconfiglib (cannot tell the fork from upstream by version).

## Cost

One extra setup command. Python and git are needed to configure Elemental
from source. Subproject consumers must provision too, or point at their own
checkout. The check script is code Elemental owns on top of the fork.

## Revisit when

- The fork publishes distinguishable versions or releases. The pin can then
  be a version, and an installed package could satisfy it.
- The fork gains strict input checking. `kconfig_check.py` should then be
  deleted in its favor.
- Requiring Python at configure time blocks a real consumer. The installed
  package and the source-drop route already avoid it; a checked-in generated
  header for a default configuration would be the next step.
