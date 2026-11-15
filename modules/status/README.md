# status

Status codes for recoverable failures, the spellings for handling one, and
the assertion used for contract violations. The vocabulary every other
module shares.

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
| [`elemental/status.h`](include/elemental/status.h) | Everyone. `el_status`, the codes, `el_status_name()`; `EL_NODISCARD`, `el_status_ignore()`; `EL_TRY`, `EL_TRY_GOTO`, `EL_TRY_NEG`, `EL_ENSURE`. | No |
| [`elemental/assert_handler.h`](include/elemental/assert_handler.h) | Applications that provide `el_assert_failed()`. | No |
| [`elemental/assert.h`](include/elemental/assert.h) | Sources that implement Elemental modules and backends. `EL_ASSERT()`. | Yes, by design |

The contracts are in the headers. The reasoning is in
[ADR 0003](../../docs/adr/0003-status-and-progress.md),
[ADR 0017](../../docs/adr/0017-negative-statuses.md), and
[ADR 0018](../../docs/adr/0018-must-use-and-propagation.md).

## Handling a status

A caller does one of four things with a status, and each has a spelling:

| | |
|---|---|
| Act on it | `if (status == EL_ERR_WOULD_BLOCK) { ... }` |
| Pass it on | `EL_TRY(el_write_all(out, data, len, NULL));` |
| Pass it on after releasing something | `EL_TRY_GOTO(el_write_all(out, data, len, NULL), status, done);` |
| Discard it on purpose | `el_status_ignore(el_buffered_writer_flush(&w));` |

Doing none of them is reported by the compiler: every function that
returns `el_status` is `EL_NODISCARD`, and so should an application's own
be. A function that finds a failure itself, in data it was given, writes
`EL_ENSURE(condition, status)`, the recoverable counterpart of
`EL_ASSERT(condition)`.

```c
enum { RECORD_ERR_TOO_LONG = -1 }; /* negative: the application's own */

EL_NODISCARD static el_status write_record(el_writer out, const char *text)
{
    const size_t len = strlen(text);
    EL_ENSURE(len <= UINT8_MAX, RECORD_ERR_TOO_LONG);

    const uint8_t header = (uint8_t)len;
    EL_TRY(el_write_all(out, &header, 1, NULL));
    return el_write_all(out, text, len, NULL);
}
```

**Negative values are the application's.** Elemental defines no negative
code and never returns one of its own, so an application numbers its
failures below zero. On Zephyr and in the Linux kernel a negative errno is
such a status as it stands: a call that returns zero or a negative errno
goes to `EL_TRY` directly, and one that returns a count or a negative errno
to `EL_TRY_NEG`. `el_status_name()` calls every negative value
`"unknown"`.

These came from the owner's `mresult` prototype. What was taken, what was
left, and the open question of context for a failure are in the
[status handling proposal](../../docs/proposals/status-handling.md).

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
| `EL_ERR_INVALID_DATA` | `frame` decode, the first module that rejects malformed input data |
| `EL_ERR_UNSUPPORTED` | the first interface with an optional operation that has no harmless null meaning |

`EL_ERR_NO_SPACE` was admitted by [ADR 0009](../../docs/adr/0009-capacity-status.md)
ahead of its first module, because `alloc` and `fmt` are built in parallel.
There is no `EL_ERR_NO_MEMORY` and no `EL_ERR_INVALID_ARGUMENT`; the ADR
says why.

Things that were considered and do not belong here: string helpers, byte
order, a logging hook. Error values that carry messages or context were on
this list; the owner reopened that question on 2026-11-15 and it is
[undecided](../../docs/proposals/status-handling.md#context-open).

The spellings above pass the rule: they are macros and one empty inline
function, so a program that does not use them carries nothing, and one that
does carries the comparison it would have written by hand.

## Tests

`ctest -R status.unit`: code values and names, and each spelling in C
functions written as an application would write them
(`tests/status_cases.c`), for Elemental's codes and an application's.

`ctest -R status.nodiscard_check` (host): using a status compiles with
`-Werror=unused-result` and dropping one is rejected, as C11, C++17, and
C23. `ctest -R headers.nodiscard` (host): every function declared to
return `el_status` is `EL_NODISCARD`.

The assertion configurations are exercised by the io module's death tests
(`io.violations`).
