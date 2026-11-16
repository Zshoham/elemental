# Proposal: handling a status

Status: First pass implemented (roadmap milestone M3b). The owner made
[decisions](#decisions) 1 and 2 on 2026-11-15; 3 to 7 are recommendations
carried out on that basis and await the owner's review.
[Context for a failure](#context) was prototyped and measured on
2026-11-16. The owner saw the recommendation and left the question open.
Author, date: design pass, 2026-11-15; context, 2026-11-16

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
[Context](#context).

## What `mresult` has, and what was taken

*Source fact:* `mresult` 1.1.0, `include/mresult.h`, a single header, read
in full on 2026-11-15 from the owner's working copy (not a repository, so
there is no revision to cite).

| `mresult` | Taken | In Elemental |
|---|---|---|
| Errors are values; no heap, no global or thread-local state | Already the rule | [conventions.md](../conventions.md#errors), rule 6 |
| `MR_TRY(expr)` | Yes | `EL_TRY(expr)` |
| `MR_TRY_GOTO(expr, errvar, label)` | Yes | `EL_TRY_GOTO(expr, status, label)` |
| `MR_CHECK(cond, code, msg)` | Yes, renamed, without the message | `EL_ENSURE(condition, status)`. In Elemental a "check" is an assertion (`ASSERT`: "Check contract assertions"), and `CHECK` aborts in other libraries. The message waits for [Context](#context). |
| `MR_TRY_NEG(expr)`; "code conventions: everything but 0 is yours" | Yes | Negative values are the application's ([ADR 0017](../adr/0017-negative-statuses.md)); `EL_TRY_NEG(expr)`. A call that returns only 0 or a negative errno goes to `EL_TRY` as it is. |
| `MR_TRYV_NEG`, `MR_TRYV` (statement expressions) | No | C11 without extensions ([ADR 0006](../adr/0006-c11-and-naming.md)). Keep the value in a variable and give that to `EL_TRY_NEG`. |
| `mr_neg(e)`, lowering to a negative errno | No | Mapping `EL_ERR_*` to errno values needs `<errno.h>`, which `status` may not depend on. It arrives with the first consumer that returns to a Zephyr API, likely M5's UART example. |
| `MR_NODISCARD`, for the user's own functions | Yes, and applied | `EL_NODISCARD` on every function that returns `el_status`, which a check enforces, and on `el_alloc` ([ADR 0018](../adr/0018-must-use-and-propagation.md)) |
| `MR_IGNORE(expr)` | Yes, as a function | `el_status_ignore(status)`: its argument is type-checked, and a macro is not needed |
| `MR_MUST(expr)`, `MR_PANIC` | No | Inside a module, a status that cannot be anything but `EL_OK` is asserted: `EL_ASSERT(status == EL_OK)`, as `io_buffered.c` now does twice. An application has its own assertion. A panic hook in `status` would be a second `el_assert_failed()`. |
| `MR_ERR(code, msg)`, `MR_ERRF`, the embedded trace, every `_CTX` form | Open | [Context](#context) |
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

8. **Context stays open.** *Owner, 2026-11-16*, after the examples and
   measurements of [Context](#context): go on to M4 first. The
   recommendation on the table is a caller-owned record
   ([ADR 0019](../adr/0019-failure-context.md), proposed, not implemented).

Decisions 3 to 7 are recommendations made on the basis of 1 and 2 and
carried out; any of them is cheap to reverse while `status` is
experimental.

## Context

What `mresult` is mostly about, and what the first pass did not provide: a
failure that says where it began and through which calls it passed.

[ADR 0003](../adr/0003-status-and-progress.md) left this out and named
what would bring it back: callers repeatedly needing context that has no
object to live on. `consume_installed` is one such caller. The owner
reopened the question on 2026-11-15 without choosing a shape; the direction
given was "a combination" of a caller-owned record and an error returned by
value.

This section is the second pass: three candidates, each written against the
same three examples, built for the Cortex-M0+ board and run on it, then
[measured](#measured). The prototype is not in the repository. The
[recommendation](#recommendation) is candidate A, written up as
[ADR 0019](../adr/0019-failure-context.md). The owner, on 2026-11-16:
leave it open and go on to M4. Nothing here is implemented.

### The candidates

**A. A caller-owned record.** `el_status` stays the return everywhere. A
function that wants a trace takes an optional `el_error *` and notes each
hop into it. A hop is a pointer to a constant *site* (file, line, a static
text) that the macro at that place defines. The record holds the status, a
count of hops, and the first four site pointers. This is the "optional
diagnostics module that wraps a status" ADR 0003 anticipates.

```c
typedef struct el_site {
    const char *file;
    const char *what;
    int32_t line;
} el_site;

typedef struct el_error {
    el_status status;        /* EL_OK: nothing recorded */
    uint32_t hop_count;      /* every hop; the first four are kept */
    const el_site *hops[4];  /* [0] is where the failure began */
} el_error;

EL_TRY_CTX(error, expr, "what was being done");
```

**B. An error returned by value**, as `mresult` does: code, message, and
trace in one struct. The prototype is `mresult` 1.1.0 itself, set as its
README advises for a small target (`MR_TRACE_DEPTH=4`, `MR_MSG_BUF=0`,
`MR_EXTERN`), with one macro added to lift an `el_status`.

**C. A two-word value**, one form of the combination: a status and a
pointer to the site where it began, returned by value, with no record and
so no chain.

```c
typedef struct el_failure {
    el_status status;
    const el_site *origin; /* static storage; null when the status is EL_OK */
} el_failure;
```

The combination proper is C for the origin plus A's record for the chain.
Its costs are both columns of the table below, so it was not built
separately.

### The examples

**Example 1: `el_cat` says which side failed.** `copy_stream` from
[examples/consume_installed/main.c](../../examples/consume_installed/main.c).

```c
/* A */
EL_NODISCARD static el_status copy_stream(el_reader in, el_writer out, size_t *copied,
                                          el_error *error)
{
    char chunk[4096];
    *copied = 0;

    for (;;) {
        size_t got;
        el_status status = el_read(in, chunk, sizeof(chunk), &got);
        if (status == EL_ERR_END_OF_STREAM) {
            return EL_OK;
        }
        EL_TRY_CTX(error, status, "reading the input");

        size_t put;
        status = el_write_all(out, chunk, got, &put);
        *copied += put;
        EL_TRY_CTX(error, status, "writing the output");
    }
}

    el_error error;
    el_error_init(&error);
    if (copy_stream(in, out, &copied, &error) != EL_OK) {
        report(console, "el_cat", &error);
    }
```

```c
/* B: the same body with these lines */
MR_NODISCARD static mr_err copy_stream(el_reader in, el_writer out, size_t *copied)
            return MR_OK;
        MR_TRY_EL(status, "reading the input");
        MR_TRY_EL(status, "writing the output");

    const mr_err error = copy_stream(in, out, &copied);
    if (mr_is_err(error)) {
        report(console, "el_cat", &error);
    }
```

```c
/* C: the same body with these lines */
EL_NODISCARD static el_failure copy_stream(el_reader in, el_writer out, size_t *copied)
            return el_no_failure();
        EL_TRY_CTX(status, "reading the input");
        EL_TRY_CTX(status, "writing the output");

    const el_failure failure = copy_stream(in, out, &copied);
    if (failure.status != EL_OK) {
        report(console, "el_cat", failure);
    }
```

All three print the same report, because the failure begins one call below
the place that reports it:

```
E el_cat: io: writing the output
  at src/ex_cat.c:24
```

**Example 2: what a log writer sink does with a failure.** One line into a
stack buffer, then `el_write_all`, as
[module-plan.md](../module-plan.md#named-by-the-owner) describes M4's
writer sink. Candidate A; the others differ only in the type of the last
parameter and the name of the function that formats it.

```c
EL_NODISCARD el_status report(el_writer out, const char *doing, const el_error *error)
{
    char storage[400];
    el_buf line;
    el_buf_init(&line, storage, sizeof(storage));

    /* A report cut short is still a report. */
    el_status_ignore(el_fmt_printf(&line, "E %s: ", doing));
    el_status_ignore(el_error_print(&line, error));
    return el_write_all(out, el_buf_data(&line), el_buf_len(&line), NULL);
}
```

**Example 3: a Zephyr driver's errno becomes a report.** Two functions
deep. The driver refuses mark parity with `-ENOTSUP` (-134), at run time.

```c
/* A */
EL_NODISCARD static el_status link_open(const struct device *uart, uint32_t baudrate,
                                        uint8_t parity, el_error *error)
{
    EL_ENSURE_CTX(error, device_is_ready(uart), -ENODEV, "the UART is not ready");
    const struct uart_config config = {
        .baudrate = baudrate,
        .parity = parity,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };
    /* uart_configure() returns 0 or a negative errno value. */
    EL_TRY_CTX(error, uart_configure(uart, &config), "configuring the UART");
    return EL_OK;
}

EL_NODISCARD static el_status link_start(const struct device *uart, el_writer out, uint8_t parity,
                                         el_error *error)
{
    EL_TRY_CTX(error, link_open(uart, 115200, parity, error), "opening the link");
    static const char hello[] = "link up\n";
    EL_TRY_CTX(error, el_write_all(out, hello, sizeof(hello) - 1, NULL), "greeting the peer");
    return EL_OK;
}
```

```c
/* B: the lines that differ */
    MR_CHECK(device_is_ready(uart), -ENODEV, "the UART is not ready");
    MR_TRY_NEG_CTX(uart_configure(uart, &config), "configuring the UART");
    MR_TRY_CTX(link_open(uart, 115200, parity), "opening the link");
    MR_TRY_EL(el_write_all(out, hello, sizeof(hello) - 1, NULL), "greeting the peer");
```

```c
/* C: the lines that differ */
    EL_ENSURE_CTX(device_is_ready(uart), -ENODEV, "the UART is not ready");
    EL_TRY_CTX(uart_configure(uart, &config), "configuring the UART");
    EL_TRY_F(link_open(uart, 115200, parity)); /* "opening the link" has nowhere to go */
    EL_TRY_CTX(el_write_all(out, hello, sizeof(hello) - 1, NULL), "greeting the peer");
```

What each printed on the board:

```
today  E link: status -134

A      E link: status -134: configuring the UART
         at src/ex_uart.c:24
         via src/ex_uart.c:31: opening the link

B      E link: status -134: uart_configure(uart, &config): configuring the UART
         at src/ex_uart.c:23
         via src/ex_uart.c:29: opening the link

C      E link: status -134: configuring the UART
         at src/ex_uart.c:24
```

A chain five calls deep printed four hops and "(1 more)" under A and B, and
the origin alone under C.

### Measured

*Measured* on 2026-11-16. **Method:** one Zephyr application outside the
repository holds the three candidates, the three examples for each, and
the same examples written with today's `EL_TRY`. It is built for
`nucleo_g0b1re_renode` (Cortex-M0+, ARMv6-M) by the Zephyr SDK 1.0.1's GCC
14.3.0 with Zephyr's flags for that board (`-Os`), and links Elemental's
`io` and `fmt`. Sizes are symbol and section sizes of the application's
objects; a function's size includes its literal pool. Code per site is the
difference between a function with nine identical sites and one with five,
divided by four, with the callee in another translation unit. Stack is
GCC's `-fstack-usage`. The reports above are an **emulator run** on that
board under Renode 1.17.0, the modified model
[build.md](../build.md#running) describes. No timing was measured.

| | Today (`EL_TRY`) | A, record | B, by value | C, two words |
|---|---|---|---|---|
| A report holds | the status | status, origin, the first four hops, a count of all | the same, and the text of the failing call | status and origin |
| Storage for it | none | 24 bytes, once, where the report is made | 60 bytes in every frame of the chain | 8 bytes in every frame |
| Stack per frame, in a chain of calls | 8 | 8 | 72 | 24 |
| Code per site that passes a failure on | 10 | 12 without context, 18 with | 22 | 14 |
| Code per site where a failure begins | 10 | 16 | 18 | 16 |
| Constant data per site with context, besides its text | 0 | 12 | 0 (file and line are operands, counted as code) | 12, origins only |
| Support code | 0 | 38 (`note` 30, `init` 8) | 62, and a `memcpy` call per hop | 0 |
| Formatting one into an `el_buf` | 40 (the status) | 40 + 132 | 40 + 180 | 40 + 60 |
| Each call that succeeds, more than today | | one argument | a hidden pointer; the callee stores three words | a hidden pointer; the callee stores two words |
| Each hop of a failure, more than today | | a call that stores three words | a call, and 60 bytes copied | two words copied |
| Example 1, object text | 103 | 228 | 224 | 212 |
| Example 3, object text | 130 | 345 | 411 | 292 |
| Example 3, stack of its top function | 48 | 80 | 176 | 56 |

Object text includes the file's name once (32 to 34 bytes here) and each
message.

**A two-word struct is not returned in registers on 32-bit Arm.** The first
pass of this proposal said candidate C "is returned in registers on the
AAPCS". It is not: the AAPCS returns a composite larger than four bytes
through memory the caller supplies, as
[ADR 0013](../adr/0013-allocation-layout.md) already records for
`el_layout`. *Measured:* a function returning `el_failure` takes a hidden
pointer in `r0` and stores both words, and its caller loads the status
back; a frame in a chain is 24 bytes against 8. On x86-64 the same struct
does come back in two registers (GCC 16.2.1, `-O2`). So on every Cortex-M
target C costs more per call than a status and a record do, and its one
claimed advantage holds only on the host.

### What the examples showed

Read from the three seats of [new-module.md](../process/new-module.md).

- **A adds a parameter; B and C change the return type.** Both touch every
  function in the chain. A's has an off position: a null record records
  nothing, so a function can be called by code that wants no trace.
- **A crosses Elemental's interfaces; B and C stop at them.** An operation
  returns `el_status` and so does the function in front of it. A failure
  that begins inside an application's own reader or writer, under
  `el_read_exact`, keeps its origin under A, because the record does not
  travel in the return value: the implementation holds the pointer in its
  own object. Under B and C the origin is lost at the operation table.
- **A's record is state.** A function that handles a failure and carries
  on (a retry after `EL_ERR_WOULD_BLOCK`, say) leaves its hops in the
  record, and has to clear it. B and C are fresh values every time. This
  is A's real friction.
- **C has no chain.** In Example 3, "opening the link" has nowhere to go.
  Adding A's record to get one is the combination, at both costs.
- **B is `mresult` and its numbers are `mresult`'s.** 60 bytes in every
  frame and copied at every hop, three stores on every success, a second
  return convention, and a break with ABI rule 8. It carries nothing A
  does not, except the text of the failing call, which `EL_TRY_CTX`'s
  text replaces.
- **A trace does not fit a short line.** Four hops with file names came to
  about 250 bytes. A sink that formats into a stack buffer cuts it, by
  the item rule; a sink that wants all of it streams through a buffered
  writer. This is M4's to settle.

### The four questions

1. **The examples**: above.
2. **Bytes per hop, and the size of the record**: the table. For A, 8
   bytes of code and 12 of constant data more than `EL_TRY` at each site
   that adds context, 2 at a site that only passes the record along, and
   one 24-byte record.
3. **Do Elemental's own functions take part?** No. They return
   `el_status` and take no record, and a backend in this repository keeps
   its detail on its object ([conventions.md](../conventions.md#errors),
   rule 6). A trace begins in the application: at the call into Elemental,
   which the site names ("writing the output"), or below Elemental, inside
   an operation the application implemented. Giving every Elemental
   function a record parameter would buy one hop the caller's site already
   states.
4. **Who renders it?** `mresult` leaves rendering to the logging layer,
   and so does this: the record is read through three accessors, and the
   function that formats one into an io buffer belongs with `log` in M4,
   whose writer sink is its first consumer. The prototype's is 132 bytes
   over `fmt`. Until then a caller walks the record, as `el_cat` does with
   `fprintf`.

### Recommendation

**Candidate A**, in the `status` module as a header of its own,
`elemental/error.h`: `el_site`, `el_error`, `el_error_init()`,
`el_error_note()`, three accessors, and a `_CTX` form of each of the four
macros. [ADR 0019](../adr/0019-failure-context.md) is the decision.

It is the combination the owner pointed at, with the by-value half kept at
one word: the status is still returned by value, and what cannot fit in a
word lives in a record the caller owns. It is the only candidate with one
return convention, the only one whose context survives an operation table,
the cheapest in stack on the targets that are short of it, and the one ADR
0003 named in advance. Its costs are the parameter and the clearing rule.

Not recommended: B, on its numbers; C, because its premise fails on
Cortex-M and it has no chain; the combination of C and A, because it pays
for two mechanisms and two return types to save the origin's first hop
from needing a record.

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

- [Context](#context): the evidence exists and a shape is recommended; the
  owner has not chosen one.
- `goto` forms of `EL_TRY_NEG` and `EL_ENSURE`: when a consumer needs them.
- Lowering a status to a negative errno: with the first consumer that
  returns to a Zephyr API.
- A human-readable sentence per status beside `el_status_name()`: not
  added, no consumer has asked.
