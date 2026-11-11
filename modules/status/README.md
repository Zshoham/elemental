# status

Status codes for recoverable failures, and the assertion used for contract
violations. The vocabulary every other module shares.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Target | `elemental::status` |
| Depends on | Nothing. No runtime dependencies. |
| Allocates | No |
| Kconfig | `STATUS`, `ASSERT`, `ASSERT_HANDLER_TRAP` / `ASSERT_HANDLER_APPLICATION` |

## Headers

| Header | For | Depends on configuration |
|---|---|---|
| [`elemental/status.h`](include/elemental/status.h) | Everyone. `el_status`, the codes, `el_status_name()`. | No |
| [`elemental/assert_handler.h`](include/elemental/assert_handler.h) | Applications that provide `el_assert_failed()`. | No |
| [`elemental/assert.h`](include/elemental/assert.h) | Sources that implement Elemental modules and backends. `EL_ASSERT()`. | Yes, by design |

The contracts are in the headers. The reasoning is in
[ADR 0003](../../docs/adr/0003-status-and-progress.md).

## Why this module is allowed to be everyone's dependency

It is the only module others may depend on without justification, so it has
an admission rule. Something belongs here only if all of these hold:

1. Nearly every module needs it.
2. It has no dependencies, no state, and no platform-specific code.
3. Its cost in an image is a few bytes.

A status code is added when a caller would handle it differently from every
existing code **and** a module in this repository returns it. Proposed codes
are listed with the module that would introduce them:

| Candidate | Would arrive with |
|---|---|
| `EL_ERR_NO_MEMORY` | allocation |
| `EL_ERR_INVALID_ARGUMENT` | the first module that validates input data rather than asserting |
| `EL_ERR_UNSUPPORTED` | the first interface with an optional operation |

Things that were considered and do not belong here: string helpers, byte
order, a logging hook, error values that carry messages or context.

## Tests

`ctest -R status.unit`: code values and names. The assertion configurations
are exercised by the io module's death tests (`io.violations`).
