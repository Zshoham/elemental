# Proposal: handling a status

Status: First pass implemented (roadmap milestone M3b). The owner made
[decisions](#decisions) 1 and 2 on 2026-11-15; 3 to 7 are recommendations
carried out on that basis and await the owner's review.
[Context for a failure](#context-open) is open.
Author, date: design pass, 2026-11-15

This changes `status` rather than adding a module. It is also the review
of the owner's earlier `mresult` prototype: what was taken from it and what
was left, as M4 asks for `mlog`.

Labels are as in [architecture.md](../architecture.md): **Source fact** is
something read in a cited source, **Measured** is a number produced in this
pass, and everything else is a recommendation.

## Problem

The owner, before the roadmap continues: improve the error handling, taking
the core ideas of `mresult`, with the focus on what it is like to use and
on completeness.

What a caller of Elemental did before this pass:

- **Passing a status on took four lines per call.** A declaration, a
  comparison, a block, and a return. *Measured:* 11 sites in the library
  and example sources did nothing else with a status, and are now one line
  each: 6 in `io` and `fmt`, 5 in the two consumer examples.
- **Nothing reported a dropped status.** A call written as a statement
  compiled without comment. *Measured:* when every declaration was made
  must-use, the libraries turned out to drop no status by accident. Four
  sites dropped one on purpose behind a `(void)` cast, three more in test
  fixtures. So the rule was being followed by hand, and nothing kept it.
- **An application had no codes of its own.** The consumer example
  returned `EL_ERR_IO` for a record too long for its one-byte length, which
  is not what `EL_ERR_IO` means.
- **A failure that reaches the top says nothing about where it came
  from.** `examples/consume_installed` gets `EL_ERR_IO` from a copy and
  asks both descriptors for an errno to guess which side failed.

This pass settles the first three. The fourth is
[Context](#context-open).

## What `mresult` has, and what was taken

*Source fact:* `mresult` 1.1.0, `include/mresult.h`, a single header, read
in full on 2026-11-15 from the owner's working copy (not a repository, so
there is no revision to cite).

| `mresult` | Taken | In Elemental |
|---|---|---|
| Errors are values; no heap, no global or thread-local state | Already the rule | [conventions.md](../conventions.md#errors), rule 6 |
| `MR_TRY(expr)` | Yes | `EL_TRY(expr)` |
| `MR_TRY_GOTO(expr, errvar, label)` | Yes | `EL_TRY_GOTO(expr, status, label)` |
| `MR_CHECK(cond, code, msg)` | Yes, renamed, without the message | `EL_ENSURE(condition, status)`. In Elemental a "check" is an assertion (`ASSERT`: "Check contract assertions"), and `CHECK` aborts in other libraries. The message waits for [Context](#context-open). |
| `MR_TRY_NEG(expr)`; "code conventions: everything but 0 is yours" | Yes | Negative values are the application's ([ADR 0017](../adr/0017-negative-statuses.md)); `EL_TRY_NEG(expr)`. A call that returns only 0 or a negative errno goes to `EL_TRY` as it is. |
| `MR_TRYV_NEG`, `MR_TRYV` (statement expressions) | No | C11 without extensions ([ADR 0006](../adr/0006-c11-and-naming.md)). Keep the value in a variable and give that to `EL_TRY_NEG`. |
| `mr_neg(e)`, lowering to a negative errno | No | Mapping `EL_ERR_*` to errno values needs `<errno.h>`, which `status` may not depend on. It arrives with the first consumer that returns to a Zephyr API, likely M5's UART example. |
| `MR_NODISCARD`, for the user's own functions | Yes, and applied | `EL_NODISCARD` on every function that returns `el_status`, which a check enforces, and on `el_alloc` ([ADR 0018](../adr/0018-must-use-and-propagation.md)) |
| `MR_IGNORE(expr)` | Yes, as a function | `el_status_ignore(status)`: its argument is type-checked, and a macro is not needed |
| `MR_MUST(expr)`, `MR_PANIC` | No | Inside a module, a status that cannot be anything but `EL_OK` is asserted: `EL_ASSERT(status == EL_OK)`, as `io_buffered.c` now does twice. An application has its own assertion. A panic hook in `status` would be a second `el_assert_failed()`. |
| `MR_ERR(code, msg)`, `MR_ERRF`, the embedded trace, every `_CTX` form | Open | [Context](#context-open) |
| `MR_RESULT(T, name)`, `MR_UNWRAP`, `mr::result<T>` | No | A status and a value are never packed into one return, and outputs are always written ([conventions.md](../conventions.md#errors), rules 1 and 4) |
| Extern mode with configuration-suffixed symbols | Not needed | No public layout depends on configuration ([ADR 0004](../adr/0004-private-configuration.md)), so there is no mismatch to catch |
| Kconfig for the knobs | Nothing to configure | This pass adds no option |
| Rendering left to the consumer | Not yet relevant | There is nothing to render but `el_status_name()` |

## Usage examples

### Example 1: a record writer and reader

From [examples/consume_subdirectory/main.c](../../examples/consume_subdirectory/main.c),
as it is now. A record is one length byte and that many bytes of text.

```c
/* The application's own failure. Negative statuses are the application's. */
enum { RECORD_ERR_TOO_LONG = -1 };

EL_NODISCARD static el_status write_record(el_writer out, const char *text)
{
    const size_t len = strlen(text);
    EL_ENSURE(len <= UINT8_MAX, RECORD_ERR_TOO_LONG);

    const uint8_t header = (uint8_t)len;
    EL_TRY(el_write_all(out, &header, 1, NULL));
    return el_write_all(out, text, len, NULL);
}

EL_NODISCARD static el_status read_record(el_reader in, char *text, size_t text_cap)
{
    uint8_t len;
    /* EL_ERR_END_OF_STREAM here means "no more records". */
    EL_TRY(el_read_exact(in, &len, 1, NULL));
    EL_ENSURE((size_t)len + 1 <= text_cap, EL_ERR_NO_SPACE);

    /* Any failure here is a record cut short. */
    EL_TRY(el_read_exact(in, text, len, NULL));
    text[len] = '\0';
    return EL_OK;
}
```

Before, `write_record` was 14 lines and `read_record` 18; they are 9 and
12. The function that treats one status as a condition compares first and
passes the rest on, as `copy_stream` in
[examples/consume_installed/main.c](../../examples/consume_installed/main.c)
does:

```c
el_status status = el_read(in, chunk, sizeof(chunk), &got);
if (status == EL_ERR_END_OF_STREAM) {
    return EL_OK; /* the one status that is not a failure here */
}
EL_TRY(status);
```

### Example 2: something to release, and a platform that returns negative errno

Zephyr: forward what a UART has received to a writer, through scratch
storage from an allocator. Not compiled in this repository: no module
includes a Zephyr driver header yet. The macros in it are compiled and
tested in `modules/status/tests/status_cases.c`.

```c
#include <zephyr/drivers/uart.h>

#include <elemental/alloc.h>
#include <elemental/io.h>

/* uart_fifo_read() returns a count, or a negative errno value. */
EL_NODISCARD static el_status forward_once(const struct device *uart, el_writer out,
                                           uint8_t *scratch, size_t cap)
{
    const int got = uart_fifo_read(uart, scratch, (int)cap);
    EL_TRY_NEG(got); /* -ENOSYS, -ENOTSUP: the application's statuses as they are */
    return el_write_all(out, scratch, (size_t)got, NULL);
}

EL_NODISCARD el_status uart_forward(const struct device *uart, el_writer out,
                                    el_allocator allocator, size_t cap)
{
    const el_layout layout = el_layout_make(cap, 1);
    uint8_t *const scratch = el_alloc(allocator, layout);
    EL_ENSURE(scratch != NULL, EL_ERR_NO_SPACE);

    const el_status status = forward_once(uart, out, scratch, cap);
    el_free(allocator, scratch, layout);
    return status;
}
```

The function that owns the storage calls one that does the work, so the
work can leave from anywhere. Where one function does both, `EL_TRY_GOTO`
keeps a single way out:

```c
    el_status status;
    EL_TRY_GOTO(el_write_all(out, head, head_len, NULL), status, done);
    EL_TRY_GOTO(el_write_all(out, body, body_len, NULL), status, done);
done:
    el_free(allocator, body, layout);
    return status;
```

**Friction found.** `EL_TRY_NEG` and `EL_ENSURE` have no `goto` form, so
inside a function with a cleanup label a negative errno is written out by
hand: `status = got < 0 ? (el_status)got : EL_OK; if (status != EL_OK)
goto done;`. The split above avoids it and is the recommended shape. The
two missing forms are added when a consumer shows the split to be worse,
not before: `mresult` has no `goto` form of either.

## Backend example

There is no backend: nothing here varies by platform. What changes for
someone implementing an interface is one permission. An operation written
by an application may return a negative status of the application's, and
the functions in front return it unchanged:

```c
static el_status sensor_read(void *ctx, void *buf, size_t cap, size_t *n)
{
    struct sensor *const sensor = ctx;
    const int got = sensor_fetch(sensor, buf, cap); /* count, or negative errno */
    EL_TRY_NEG(got);
    EL_ENSURE(got > 0, EL_ERR_WOULD_BLOCK);
    *n = (size_t)got;
    return EL_OK;
}
```

A backend that ships in Elemental does not do this. It returns `EL_ERR_IO`
and keeps the detail on its object, as `io_posix` does.

## Why not something that exists

- **Plain `if`.** It is what the macros expand to, and it stays correct
  and allowed. The cost was length at every call, which is what makes a
  missing check hard to see.
- **A `(void)` cast for a deliberate discard.** GCC keeps warning through
  it for `warn_unused_result`, which is the attribute available in C11.
  *Measured* with GCC 16.2.1 and Clang 23.1.5: Clang accepts the cast in
  every mode, GCC only for `[[nodiscard]]` (C23, C++17).
- **`mresult` itself, vendored.** Its error is a struct of 16 to 176 bytes
  returned by value on every call, with a layout chosen by configuration
  macros. Elemental's rules are a 32-bit status, at most two words by
  value, and no layout that depends on configuration
  ([conventions.md](../conventions.md#abi), rules 3 and 8).

## Dependencies and cost

| | |
|---|---|
| Elemental modules | None. `status` gains no dependency. |
| Runtime symbols (freestanding) | None |
| Allocation | None |
| Per-object storage | None: there is no new type |
| New status codes | None. Negative values are reserved for applications. |
| Code size | See [Verification](#verification): the macros expand to the comparison that was written by hand, and `el_status_ignore()` is an empty inline function |

## Contracts

- **Ownership and lifetime:** nothing is owned or borrowed.
- **Errors:** `EL_TRY` and `EL_TRY_GOTO` treat every status other than
  `EL_OK` alike, `EL_ERR_WOULD_BLOCK` and `EL_ERR_END_OF_STREAM` included.
  `EL_TRY_NEG` treats zero and positive values as success and never
  inspects them.
- **Negative values:** Elemental defines none and never returns one of its
  own. One reaches a caller only from application code, directly or through
  an operation the application implemented. `el_status_name()` calls it
  `"unknown"`.
- **Concurrency and execution context:** the macros touch only what their
  arguments name. Usable wherever the surrounding code is.
- **Evaluation:** each macro evaluates its expression exactly once and is
  one statement. `EL_ENSURE` evaluates its status only when the condition
  fails.
- **Exceptions to docs/conventions.md:** none. ABI rule 6 allows a macro
  for what only a macro can do, and leaving the enclosing function is
  that. `el_status_ignore()` is the first `static inline` function in a
  public header; `bytes` rejected inline-only functions because another
  language has nothing to link, and this one has nothing another language
  needs.

## Interface model

No interface, no instance, nothing decided at build time. The spellings
are in `elemental/status.h` rather than a header of their own: every
module already includes it, and a second header would only add an include
to each file that passes a status on.

## Configuration

None. `EL_NODISCARD` depends on the compiler and language mode, not on
Elemental's configuration.

## Decisions

1. **Start with the syntax; context comes later.** *Owner, 2026-11-15:*
   the direction for context "might be a combination" of a caller-owned
   record and an error returned by value, shape not yet known; "start with
   the syntax, like `EL_TRY` and similar macros".
2. **Negative values belong to the application.** *Owner, 2026-11-15.*
   [ADR 0017](../adr/0017-negative-statuses.md).
3. **Every function whose result reports a failure is `EL_NODISCARD`**:
   those returning `el_status`, and `el_alloc`. Operation-table members
   are not: the standard attribute cannot be put on a function pointer,
   and only the front functions call them.
   [ADR 0018](../adr/0018-must-use-and-propagation.md).
4. **A deliberate discard is `el_status_ignore()`.**
5. **Four macros, in `status.h`:** `EL_TRY`, `EL_TRY_GOTO`, `EL_TRY_NEG`,
   `EL_ENSURE`. No `goto` forms of the last two yet.
6. **Not taken:** `MR_MUST`, result types, statement-expression forms,
   lowering to errno. Reasons in the table above.
7. **Elemental's own sources use them** where a status is only passed on:
   six sites in `io` and `fmt`. A site that compares against a particular
   status keeps its `if`.

Decisions 3 to 7 are recommendations made on the basis of 1 and 2 and
carried out; any of them is cheap to reverse while `status` is
experimental.

## Context (open)

What `mresult` is mostly about, and what this pass does not provide: a
failure that says where it began and through which calls it passed.

[ADR 0003](../adr/0003-status-and-progress.md) left this out and named
what would bring it back: callers repeatedly needing context that has no
object to live on. `consume_installed` is one such caller. The owner has
reopened the question and has not chosen a shape. The candidates, none of
them built:

**A. A caller-owned record.** `el_status` stays the return everywhere. A
function that wants a trace takes an optional `el_error *` and records
each hop into it: file, line, a static context string. Fixed size, no
heap. This is the "optional diagnostics module that wraps a status" ADR
0003 anticipates.

```c
EL_NODISCARD static el_status load(el_reader in, config *out, el_error *err)
{
    EL_TRY_CTX(err, read_header(in, &header, err), "reading the header");
    ...
}
```

Costs: one more parameter on every function in the chain, and Elemental's
own functions do not take it, so a trace begins at the first application
frame.

**B. An error returned by value**, as `mresult` does: code, message, and
trace in one struct. Costs: 100 bytes or more copied on every return,
success included; a second return convention beside `el_status`; and a
break with ABI rule 8.

**C. A combination**, the owner's stated direction. One form it could
take: a two-word value, a status and a pointer to a constant *origin*
(file, line, message) that the macro at the failing site defines, with a
caller-owned record of kind A for the chain when one is wanted.

```c
typedef struct el_failure {
    el_status status;
    const el_origin *origin; /* static storage; null when the status is EL_OK */
} el_failure;
```

It fits ABI rule 8 and is returned in registers on the AAPCS and on
System V x86-64. It says where a failure began and why at no run-time
cost beyond one register. It still makes two return types, and
Elemental's own functions would produce no origin.

What a decision needs, in the order the module process asks for it:

1. The same three examples written against each candidate: `el_cat`
   reporting which side failed; M4's writer sink printing a failure; a
   Zephyr function that turns a driver's errno into a report.
2. *Measured* on the Cortex-M0+ board: bytes per hop, and the size of the
   record.
3. Whether Elemental's own functions take part, or the trace begins in
   the application.
4. Who renders it. `mresult` leaves that to the consumer; Elemental has
   `fmt`, and `log` arrives in M4.

Nothing in this pass has to change for any of the three. The four macros
take no context argument, so a `_CTX` family or a record parameter is an
addition.

## Verification

- **Unit tests** (`status.unit`, also in the Zephyr `status` image): each
  macro in C functions written as an application would write them
  (`status_cases.c`), for every Elemental code and an application's:
  returns the first status that is not `EL_OK`, evaluates once, is one
  statement, `EL_TRY_GOTO` assigns either way, `EL_TRY_NEG` continues on
  zero and on a count wider than `el_status`, `EL_ENSURE` evaluates its
  status only on failure; and the same macros compiled as C++17.
- **Must-use** (`status.nodiscard_check.*`, host): using, passing on, and
  ignoring a status compiles with `-Werror=unused-result`; a call that
  drops one is rejected with that diagnostic. Once per spelling of the
  attribute: C11, C++17, and C23 where the compiler has it.
- **Every declaration** (`headers.nodiscard`, host): each function under
  `modules/` and `examples/` declared to return `el_status` carries
  `EL_NODISCARD`.
- **Negative statuses pass through**: `io.helpers` for the four io
  functions, `io.unit.buffered` for the buffered writer.
- **Broken on purpose**, with `mutant.sh`: recorded in
  [build.md](../build.md#what-is-verified).

| Target | Toolchain | Level |
|---|---|---|
| x86-64 Linux | GCC, Clang, Clang with sanitizers | Run |
| `native_sim`, `mps2/an385`, `nucleo_g0b1re_renode` | Zephyr SDK GCC | Emulator run of `status.unit`; the must-use checks are host only |

## Evidence required before stable

As for `status` as a whole. Specific to this pass: the names have been
used by one module outside `io` and `fmt` (M4's `time` or `log`), and the
context decision is made, because it may add a parameter or a family to
these macros.

## Open questions

- [Context](#context-open).
- `goto` forms of `EL_TRY_NEG` and `EL_ENSURE`: when a consumer needs them.
- Lowering a status to a negative errno: with the first consumer that
  returns to a Zephyr API.
- A human-readable sentence per status beside `el_status_name()`: not
  added, no consumer has asked.
