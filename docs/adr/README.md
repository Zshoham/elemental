# Decision records

Short records for decisions that would otherwise be reopened. Each states the
context, the decision, what it costs, and what evidence would justify
changing it. A record is changed by adding a new one that supersedes it, not
by editing history.

| # | Decision | Status |
|---|---|---|
| [0001](0001-scope.md) | Elemental replaces selected APIs; it is not a libc or a runtime | Accepted |
| [0002](0002-interface-model.md) | Concrete types first; handles where backends coexist; link-time only for singletons | Accepted |
| [0003](0003-status-and-progress.md) | Integer status codes; progress or status, never both | Accepted; refined by 0017 and 0018 |
| [0004](0004-private-configuration.md) | Configuration is private to the implementation | Accepted; refined by 0016; one exception, 0021 |
| [0005](0005-kconfig-dependency.md) | Kconfig through a pinned fork, provisioned explicitly | Accepted |
| [0006](0006-c11-and-naming.md) | C11 baseline and the `el_` prefix | Accepted |
| [0007](0007-first-slice.md) | The first slice is status and byte I/O | Accepted; its freestanding contract suite is superseded by 0008, its freestanding cross build by 0010 |
| [0008](0008-googletest.md) | Tests are GoogleTest; target tests go through Zephyr | Accepted; its `cortex-m3` preset is superseded by 0010 |
| [0009](0009-capacity-status.md) | One capacity status, `EL_ERR_NO_SPACE` | Accepted |
| [0010](0010-zephyr-targets.md) | Microcontroller targets are Zephyr targets | Accepted |
| [0011](0011-formatting-truncation.md) | Formatting writes to caller memory; truncation keeps a prefix that ends on an item | Superseded by 0015 |
| [0012](0012-sized-free.md) | Free takes the size and alignment back | Accepted; refined by 0013 |
| [0013](0013-allocation-layout.md) | An allocation is described by a layout | Accepted |
| [0014](0014-flush-on-the-concrete-type.md) | Flush is a function of the buffered writer, not an operation of `el_writer` | Accepted; confirmed by M4's writer sink |
| [0015](0015-formatting-into-io-buffers.md) | Formatting appends to an io buffer; output is a prefix that ends on an item | Accepted |
| [0016](0016-private-choices-that-widen.md) | A private choice may widen what a function accepts, never change it | Accepted |
| [0017](0017-negative-statuses.md) | Negative status values belong to the application | Accepted |
| [0018](0018-must-use-and-propagation.md) | A status must be used; passing one on is spelled with macros | Proposed and implemented; awaiting the owner's review |
| [0019](0019-failure-context.md) | Context for a failure lives in a record the caller owns | Proposed; left open by the owner, not implemented |
| [0020](0020-time-representation.md) | Time is a 64-bit count of a clock's own ticks | Accepted |
| [0021](0021-log-cutoff.md) | The log cut-off is a macro the consumer defines | Accepted; the first exception to 0004 |
| [0022](0022-log-sink-lends-the-buffer.md) | A log sink lends the buffer a record's message is written into | Accepted |
| [0023](0023-atomics-and-interrupt-context.md) | Atomics are loads and stores of one word; an interrupt handler is one more context | Accepted |
| [0024](0024-el-buf-keeps-what-it-holds.md) | An `el_buf` keeps what it holds | Accepted |
| [0025](0025-an-allocation-changes-size-and-may-move.md) | An allocation changes size through one optional operation that may move it | Accepted |

To add one, copy the shape of an existing record: Context, Decision, Cost,
Revisit when. Keep it under a page.
