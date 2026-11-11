# Decision records

Short records for decisions that would otherwise be reopened. Each states the
context, the decision, what it costs, and what evidence would justify
changing it. A record is changed by adding a new one that supersedes it, not
by editing history.

| # | Decision | Status |
|---|---|---|
| [0001](0001-scope.md) | Elemental replaces selected APIs; it is not a libc or a runtime | Accepted |
| [0002](0002-interface-model.md) | Concrete types first; handles where backends coexist; link-time only for singletons | Accepted |
| [0003](0003-status-and-progress.md) | Integer status codes; progress or status, never both | Accepted |
| [0004](0004-private-configuration.md) | Configuration is private to the implementation | Accepted |
| [0005](0005-kconfig-dependency.md) | Kconfig through a pinned fork, provisioned explicitly | Accepted |
| [0006](0006-c11-and-naming.md) | C11 baseline and the `el_` prefix | Accepted |
| [0007](0007-first-slice.md) | The first slice is status and byte I/O | Accepted; its freestanding contract suite is superseded by 0008, its freestanding cross build by 0010 |
| [0008](0008-googletest.md) | Tests are GoogleTest; target tests go through Zephyr | Accepted; its `cortex-m3` preset is superseded by 0010 |
| [0009](0009-capacity-status.md) | One capacity status, `EL_ERR_NO_SPACE` | Accepted |
| [0010](0010-zephyr-targets.md) | Microcontroller targets are Zephyr targets | Accepted |
| [0012](0012-sized-free.md) | Free takes the size and alignment back | Accepted |

To add one, copy the shape of an existing record: Context, Decision, Cost,
Revisit when. Keep it under a page.
