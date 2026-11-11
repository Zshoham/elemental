# Elemental architecture

This is the design proposal for Elemental: what it is responsible for, how it
is divided, how interfaces and backends relate, and where the next modules are
likely to go. It is written to be argued with. Each decision states the
problem, the choice, what the choice costs, and what evidence should reopen
it. Decisions that would otherwise be reargued repeatedly have a short record
in [adr/](adr/README.md).

Three labels are used throughout, and they mean different things:

- **Implemented**: in this repository, built and tested. See
  [build.md](build.md#what-is-verified) for exactly what was run.
- **Proposed**: a direction with examples, not code. Sections 6.2 to 6.5.
- **Source fact**: something read in a reference project, cited in
  [section 7](#7-sources). Everything else is a recommendation.

Companion documents: [conventions.md](conventions.md) holds the C contract
rules, [build.md](build.md) the build and configuration mechanics. They are
not repeated here.

## 1. Project boundaries

### What Elemental is

A set of small C interfaces, each with a written contract, a portable
implementation where one can exist, and a test suite that states the contract
in executable form. The interfaces and implementations are C; the tests are
GoogleTest ([ADR 0008](adr/0008-googletest.md)). The target range is native applications, embedded Linux,
and microcontrollers, from one source tree.

Elemental owns:

- **Interface contracts.** Types, functions, and their documented behavior:
  ownership, errors, blocking, concurrency.
- **Portable implementations** that need nothing from a platform, such as
  streams over caller-provided memory.
- **Contract test suites**, written as GoogleTest tests and shipped as
  source, so that implementations outside this repository can run them.
- **Reference backends** for common platforms (today: POSIX file
  descriptors), kept only where they are tested.

Elemental does not own:

| Belongs to | What |
|---|---|
| The application | `main`, startup order, which backend each component is given, threads, the event loop, every policy decision. Elemental never starts a thread, installs a signal handler, or reads global state on its own. |
| A platform port | Startup code, vector tables, linker scripts, clocks, interrupt wiring, the C runtime the compiler requires. |
| A backend | The mapping from one interface onto one platform facility. A backend may live in this repository or outside it. |
| External libraries | Protocols, parsers, cryptography, compression, anything with a domain of its own. |

### What "libc replacement" means here

It means **replacing selected APIs**: giving a program a better interface than
`read`/`write`/`errno`, `malloc`/`free`, or `printf`-to-a-global-stream for
the jobs those do. A program that uses Elemental still links whatever C
library its platform has, or none.

It does not mean, and Elemental does not promise:

- a freestanding C runtime, startup code, or linker scripts;
- compiler support routines (`memcpy` and friends, `__aeabi_*`, soft-float,
  64-bit division helpers);
- ISO C or POSIX compatibility, or drop-in replacements for libc symbols.

The evidence so far supports exactly this much. The freestanding libraries of
the first slice leave one undefined symbol, `memcpy`. An earlier revision
also linked a bare test image for a Cortex-M3 without a C library. That
required, from the test and not from Elemental: startup code, a vector
table, a linker script, the four `mem*` functions, and the ARM run-time
ABI's `__aeabi_mem*` helpers that Clang emits for ordinary C. That list is
the honest size of "a runtime", and none of it is in scope. The image was
removed when the tests moved to GoogleTest; target testing is planned
through Zephyr, which supplies exactly that runtime. See [ADR 0001](adr/0001-scope.md).

*Revisit if* several ports end up carrying identical copies of the same
runtime shim. That would be evidence for a small, optional, separately named
runtime-support module, not for widening this definition.

## 2. Module structure and independence

### Layout

```
CMakeLists.txt  Kconfig         the single build entry and the Kconfig root
cmake/                          configuration, module helper, package, toolchains
modules/
  status/                       status codes, contract assertions
    Kconfig  CMakeLists.txt  include/elemental/  src/  tests/
  io/                           reader/writer interfaces, memory streams
    Kconfig  CMakeLists.txt  include/elemental/  src/  tests/
    contract/                   the io contract suite (GoogleTest source)
    backends/posix/             a platform backend, with PORT.md
examples/                       a backend, and two consumer projects
tests/                          repository-level checks: headers, interop,
                                integration, cross
configs/                        defconfigs
docs/
```

A module is a directory that carries everything about itself: its Kconfig,
build file, public headers, sources, tests, and README. Adding a module does
not touch another module's files; it adds one line to the module list in
`CMakeLists.txt` and one `rsource` line in `Kconfig`.

### The four kinds of code

The distinction is kept where it does work, and only there.

| Kind | What it is | Where | In the slice |
|---|---|---|---|
| Interface contract | Types, operations, documented behavior | `modules/<m>/include/elemental/<m>.h` | `io.h` |
| Portable implementation | Needs nothing from a platform | same module | `io_mem.h`, `io_mem.c` |
| Platform backend | One interface on one platform facility, its own library | `modules/<m>/backends/<platform>/` | `io_posix` |
| Application composition | Chooses backends and wires them | the application | `examples/consume_*` |

There is no "core", "common", "hal", or "port" directory, because nothing
needs one yet. A platform backend is a library next to its interface, not a
layer under it.

### Dependency graph

```
status          (no dependencies)
  ^
  io            (status; memcpy)
  ^        ^
io_posix   io_contract
(POSIX)    (test support)
```

`status` is the only module every other module may depend on. That privilege
is why it must stay tiny; its admission rule is in
[modules/status/README.md](../modules/status/README.md). Any other
module-to-module dependency has to be justified in the module's proposal.

### What "standalone module" means

| Aspect | Guarantee |
|---|---|
| **Source consumption** | A module is `include/` plus `src/`, compiled with any C11 compiler and one extra header, `elemental_config.h`, on the include path. No generated code other than that header, which may be written by hand. |
| **CMake use** | One target per module, `elemental::<module>`, a static library. Linking it brings its dependencies and nothing else. With `add_subdirectory(... EXCLUDE_FROM_ALL)` only linked modules are compiled. |
| **Generated configuration** | Private to Elemental's own sources. Public headers never include it, so a consumer's sources compile the same way whatever the configuration is. |
| **Transitive dependencies** | Stated in the module's README and enforced by the build: a module names its dependencies, and a missing one is a configure-time error. Today the longest chain is two (`io_posix` → `io` → `status`). |

No module requires a global runtime, an init call, or another module that it
does not name. The three supported ways to consume a module, and which of them
need Kconfig, are in [build.md](build.md#consuming-elemental).

**Cost.** One static library per module means several small archives rather
than one, and the repository root, not a module directory, is the CMake entry
point. Supporting `add_subdirectory(modules/io)` directly would need each
module to locate its dependencies and its configuration alone, which is a
second code path for no demonstrated need.

*Revisit if* consumers vendor single module directories often enough that the
"source consumption" route deserves a build file of its own.

## 3. Interfaces and backends

### The problem

Code that reads bytes should not care where they come from. But the obvious
ways to hide the source each cost something: a function table costs an
indirect call and a lifetime rule; link-time substitution allows only one
implementation per program; a universal object model costs everyone
everything. Choosing one mechanism for all modules would overpay somewhere.

### The rule

Use the weakest mechanism that does the job, in this order
([ADR 0002](adr/0002-interface-model.md)):

**1. Plain functions on caller-owned objects. The default.** Most code needs
no backend at all. A memory writer is a struct and four functions; there is
nothing to select.

```c
el_mem_writer writer;
el_mem_writer_init(&writer, frame, sizeof(frame));
```

**2. A handle: context pointer plus a constant table of operations.** Use it
when implementations must coexist in one program and the caller must not know
which one it has. This is the only form of runtime polymorphism in Elemental.

The consumer's side, from
[examples/consume_subdirectory/main.c](../examples/consume_subdirectory/main.c):

```c
static el_status write_record(el_writer out, const char *text)
{
    const uint8_t header = (uint8_t)strlen(text);
    const el_status status = el_write_all(out, &header, 1, NULL);
    if (status != EL_OK) {
        return status;
    }
    return el_write_all(out, text, header, NULL);
}
```

The implementer's side, from
[examples/backend_ring/ring_pipe.c](../examples/backend_ring/ring_pipe.c):

```c
static el_status ring_write(void *ctx, const void *data, size_t len, size_t *n)
{
    ring_pipe *const pipe = ctx;
    if (pipe->write_closed) {
        return EL_ERR_END_OF_STREAM;
    }
    const size_t space = pipe->capacity - pipe->count;
    if (space == 0) {
        return EL_ERR_WOULD_BLOCK;
    }
    /* ... copy min(len, space) bytes ... */
    *n = count;
    return EL_OK;
}

static const el_writer_ops ring_writer_ops = {.write = ring_write};

el_writer ring_pipe_as_writer(ring_pipe *pipe)
{
    return (el_writer){.ctx = pipe, .ops = &ring_writer_ops};
}
```

What makes this pleasant for the implementer is that the operation has no
argument checking and no bookkeeping for the failure paths. The public
function in front of it (`el_write`) validates the caller's arguments
before the operation runs and checks the operation's result after, so each
side is held to the contract at the boundary.

**3. A function the application defines, resolved by the linker.** Use it
only for a hook that is naturally one-per-program and has to be reachable
without a handle. There is exactly one today: the assertion failure handler.

```c
void el_assert_failed(const char *file, int line)   /* the application's */
{
    log_fault(file, line);
    system_reset();
}
```

**4. Operations supplied by the caller for one call.** Not an interface
object, just parameters. The contract suite takes its fixture this way.

And before any of these: **prefer not to call out at all.** Logic that can be
written as "bytes in, results out" needs no interface for its environment.
See [section 5](#5-resources-and-environmental-effects).

### Compile-time policy and runtime state are separate

| Decided at | By | Examples |
|---|---|---|
| Build time | Kconfig | Which modules and backends exist in the build. Whether contract assertions are compiled, and what a failure does. |
| Run time | The application | Which object a component is handed. How many there are. |

Configuration never selects *the* backend of an interface. It selects which
backends are available; the application picks one per instance.

**Several instances and backends coexist by construction.** State lives in
objects the caller owns, and a handle names one object, so nothing is shared.
This is exercised: the POSIX contract test runs two fixtures over different
descriptors in one program, and the consumer examples use memory streams
next to a file descriptor.

**Where exactly one is supported, it is deliberate and visible.** The
assertion handler is one per program, because a failed assertion has no
object to dispatch on. The rule for adding another singleton is the same:
there must be no instance to carry the choice.

### What was considered and not chosen

- **The interface struct embedded in the implementation**, recovered with
  `container_of`. *Source fact:* Zig's `std.Io.Reader` holds its vtable and
  buffer and implementations recover themselves from it; Zephyr drivers hang
  an API struct off `struct device`. It saves a pointer and allows a
  non-virtual buffered fast path. It was not chosen because the pointer
  arithmetic is awkward to express from Rust and Python, and nothing here is
  buffered yet. *Revisit if* a buffered reader shows the indirect call per
  byte is measurable.
- **Header-substitution facades.** *Source fact:* Pigweed resolves facades at
  compile time, and its own guidance is to prefer dependency injection where
  its runtime cost is affordable, then link-time substitution, and to write a
  facade only when a backend must provide a header. Elemental follows that
  ordering and has no case yet that reaches the third step.
- **One universal object model** for every module. Rejected outright: it is
  the abstraction the project's priorities argue against.

**Cost of the rule.** A handle call is indirect and cannot be inlined across
the boundary. Handles carry a lifetime rule the compiler does not check. And
"weakest mechanism" means modules will not look uniform, which costs some
learnability.

## 4. C contracts and interoperability

Covered in [conventions.md](conventions.md): language baseline, naming,
ownership, storage, errors, argument validity, concurrency, assertions, ABI,
and use from C++, Rust, and Python.

## 5. Resources and environmental effects

### Storage is the caller's

No function in the slice allocates. Objects have public layouts so that the
caller can place them in static, stack, or heap storage, and buffers are
passed in. This is not a temporary state of affairs. The rule for future
modules is:

- A module that can work in caller-provided storage must, with no allocator
  in its signature.
- A module that genuinely needs dynamic storage takes an allocator handle as
  an explicit parameter. Nothing reaches for a global heap.
- Convenience that needs allocation (a growable buffer writer, say) goes in a
  separate library, so the base module does not acquire a dependency on the
  allocation module.

Which candidate modules need allocation:

| Module | Needs an allocator? |
|---|---|
| status, io | No. |
| allocation | It is one. Its arena runs over caller storage. |
| logging | No. Formatting into a caller buffer; deferred logging into a caller-provided ring. |
| async | Not in the interface. Operation state is caller-owned; an executor's queues are sized from caller storage. |
| IPC | No for framing. Transport-specific for the rest. |

*Source fact:* TigerStyle requires that all memory be statically allocated at
startup. Elemental takes the weaker, enforceable form: a module never
allocates behind the caller's back, so an application that wants the strong
form can have it.

### Limits are explicit

Loops in the slice have a stated bound (`el_read_exact` runs at most `len`
attempts, because every attempt that continues moved a byte). Nothing waits,
retries, or sleeps on its own; a stalled stream is reported to the caller,
who is the only one who knows what waiting should mean.

### Logic, separated from effects

There are two different things, and Elemental uses both for different jobs.

**An I/O abstraction** inverts a dependency: code calls `el_read` instead
of `read`. `el_reader` and `el_writer` are this. It makes code portable
and testable against a fake. It does not make the code free of I/O: the code
still decides when I/O happens, and still has to handle a stream that stalls
in the middle of whatever it was doing. **Putting system calls behind
function pointers is not Sans-I/O**, and nothing in this repository claims
otherwise.

**Sans-I/O** removes the call altogether. *Source fact:* the Sans-I/O
guidance defines such an implementation as one "that contains no code that
does any form of network I/O or any form of asynchronous flow control": bytes
are handed in, and events and bytes to send are handed back. The caller owns
every effect. A component built this way has no notion of blocking, needs no
fake, and its tests are plain function calls.

The rule: **write logic as bytes-in, results-out wherever it can be, and use
the io interfaces only in the thin layer that connects it to a stream.** A
frame decoder (section 6.5) should be a function of its input. A helper that
reads frames from an `el_reader` is glue on top, and is where the
would-block handling goes, once.

The same applies to other environmental inputs. Time is a parameter, not a
call: a module that needs "now" is given a timestamp or a clock operation by
its caller. No module reads a clock, an environment variable, or a global.

**Cost.** Bytes-in, results-out APIs are more work to design and push
buffering onto the caller. They are worth it for protocol-shaped logic and
overkill for a ten-line helper, so this is a preference with judgement, not
a law.

## 6. Module directions

### 6.1 Errors and status — implemented

[modules/status](../modules/status/README.md). A status is a 32-bit integer
from a small shared vocabulary; contract violations are assertions, not
statuses ([ADR 0003](adr/0003-status-and-progress.md)).

The vocabulary has four codes because the slice returns four. Richer error
values, carrying a message, a context chain, or a source location, were
considered and left out of the shared vocabulary: they make every return
larger, they pull formatting toward the bottom of the dependency graph, and
what a caller branches on is still the code. Detail stays with the object
that failed (`el_posix_fd_last_errno`). *Source fact:* Zig's reader reports
`ReadFailed` and directs callers to the implementation for diagnostics.

*Revisit if* callers repeatedly need to carry context across layers that have
no object to hang it on. That would be a case for an optional diagnostics
module that wraps a status, not for growing the status.

### 6.2 Allocation — proposed

The next module. A handle, because allocators must coexist (an arena for a
request, a pool for messages, the heap for the rest).

```c
typedef struct el_allocator_ops {
    void *(*alloc)(void *ctx, size_t size, size_t align);
    void (*free)(void *ctx, void *ptr, size_t size, size_t align);
} el_allocator_ops;

typedef struct el_allocator {
    void *ctx;
    const el_allocator_ops *ops;
} el_allocator;

void *el_alloc(el_allocator a, size_t size, size_t align);
void el_free(el_allocator a, void *ptr, size_t size, size_t align);
```

Usage, and the awkwardness it exposes:

```c
static uint8_t storage[4096];
el_arena arena;
el_arena_init(&arena, storage, sizeof(storage));
el_allocator a = el_arena_as_allocator(&arena);

struct session *s = el_alloc(a, sizeof(*s), _Alignof(struct session));
if (s == NULL) { /* exhausted: the one way this fails */ }
...
el_free(a, s, sizeof(*s), _Alignof(struct session));   /* must repeat both */
```

Positions to test, each with its cost:

- **Alignment is always explicit**, a power of two. No "suitable for any
  type" default, which wastes memory on small targets.
- **Free takes the size and alignment back.** An arena or pool then needs no
  per-allocation header. *Source fact:* Zig's allocator `free` receives the
  slice and the alignment; Pigweed's `Deallocate` takes only the pointer.
  The cost is on callers, who must know the size, which is uncomfortable for
  strings and anything handed across an API. This is the main thing the
  module's examples must put under pressure.
- **Exhaustion returns `NULL`.** Allocation has one recoverable failure and a
  pointer result, so a status plus `void **` would be worse: passing a typed
  pointer's address as `void **` is not valid C. This is the documented
  exception to "status return, value by output parameter"
  ([conventions.md](conventions.md#errors)). `EL_ERR_NO_MEMORY` joins the
  vocabulary for functions that allocate among other things.
- **Storage ownership.** Memory is released through the allocator that
  provided it. A component that allocates stores the handle it was given;
  it never assumes a default.
- **Open:** resize in place, array sizes with overflow checking, what an
  allocator does when a caller frees a pointer it does not own.

### 6.3 Byte I/O — implemented, with known gaps

[modules/io](../modules/io/README.md). How it answers the four questions that
make stream interfaces go wrong:

| Question | Answer |
|---|---|
| Partial progress | Normal, and success. One attempt moves between 1 and the requested count. Helpers that loop report the status and the progress so far, so they are resumable. |
| End of stream | A status, never a zero count. A zero count would be ambiguous with a zero-length request, and "zero means end" is the classic mistake. It is permanent once reported. For a writer it means "no more can ever be accepted", which covers a full fixed buffer and a vanished peer alike. |
| Would-block | A status with zero progress. Whether a stream can stall is a property of the stream, chosen by whoever created it. The interface does not have a blocking mode. |
| Buffer lifetime | Borrowed for the call. An implementation copies what it takes and may not keep the pointer. The contract suite checks this by overwriting the buffer after the call. |

*Source facts* that informed it: Zig's reader reports end of stream as an
error and states that a zero count "does not indicate end of stream";
Pigweed's stream returns `OUT_OF_RANGE` at the end of a reader or writer and
`RESOURCE_EXHAUSTED` when nothing can be read now.

Deliberately absent, to be added only with a consumer that needs them:
flush, buffered reading and peek, vectored I/O, seek, and close. Deadlines
are not coming to this interface at all: a deadline needs a clock, and the
caller already has the means to stop calling.

### 6.4 Logging — proposed

The interesting tension is between a good C call site and a contract other
languages can use.

```c
/* The contract: not variadic, no global, usable from any language. */
void el_log_write(el_logger log, el_log_level level,
                    const char *message, size_t len);

/* A C convenience on top, in its own header. Not part of the contract. */
EL_LOG_INFO(log, "connected to %s:%d", host, port);
```

- A sink is a handle: several coexist. A process-wide default logger is an
  application's choice to build, not something the module provides.
- Formatting is a separate concern that writes to an `el_writer`, so a
  message can be formatted into a stack buffer with no allocation.
- **Compile-time level filtering is the first real exception to private
  configuration** ([ADR 0004](adr/0004-private-configuration.md)): to remove
  a call site, the caller's translation unit must see the level. The
  proposal must choose between a compile definition carried by the CMake
  target and a public generated header, and justify it.
- A timestamp is an argument or a caller-supplied operation; the logger does
  not read a clock.
- Use from an interrupt handler means deferring, into a caller-provided ring.

### 6.5 Async and IPC — discovery before design

Neither gets a public header until a discovery milestone has answered the
questions below with prototypes. The reason is visible in a first sketch:

```c
typedef struct el_op el_op;   /* caller-owned */
typedef void el_op_done_fn(void *ctx, el_op *op, el_status status, size_t n);

el_status el_async_read(el_async_reader r, el_op *op,
                            void *buf, size_t cap,
                            el_op_done_fn *done, void *ctx);
```

`op` and `buf` must stay valid and must not move until `done` runs. That
contradicts two rules the rest of Elemental relies on: buffers are borrowed
for the duration of a call, and objects may be moved when no handle is in
use. It also maps badly onto Rust's borrow rules and Python's reference
counting. An async module is therefore a different contract, not an
extension of `io`, and the cost of getting it wrong is high.

**Async: questions to resolve.**

- *Operation lifecycle.* Who owns operation state, and for how long? When may
  it be reused? Is "submitted" distinguishable from "started"?
- *Completion.* Exactly once? On which context: the caller's, the
  executor's, an interrupt? By callback, or by polling a completed set?
- *Cancellation.* A request, or a guarantee? Is the completion still
  delivered, and with which status? When do borrowed buffers come back?
- *Executor ownership.* The application creates and runs it; Elemental never
  starts a thread. Can a component be written without knowing which executor
  it runs on?
- *Bounded resources.* What is the limit on in-flight operations, where does
  the storage come from, and what does submission return when it is full?
- *Model.* Readiness (retry when ready) and completion (the operation owns
  the buffer until done) are different contracts. The non-blocking `io`
  contract is already readiness-shaped. Is a completion model needed, and
  for which backends?

*Source fact:* Zig's `std.Io` interface has `async`, `concurrent`, `await`,
and `cancel` operations in which the caller describes the result storage and
the context is copied, and most of its operations can report cancellation.
An earlier prototype by the project's author, `myio`, implemented an
awaitable-task model over seven backends; its lessons should be an input to
discovery. It was not reviewed for this milestone.

**IPC: what can be portable, and what cannot.**

| Portable | Transport-specific |
|---|---|
| Message framing over a byte stream | Naming, addressing, discovery |
| A maximum message size, fixed up front | Connection setup and teardown, reconnection |
| Request and response correlation | Credentials and permissions |
| Backpressure, as would-block | Passing descriptors or handles |
| | Shared memory and zero-copy |
| | Ordering and delivery guarantees; native message boundaries |

The portable column is small, and most of it is a codec. The likely shape is
therefore a Sans-I/O framing module:

```c
/* Bytes in; at most one complete message out. No reader, no clock. */
el_status el_frame_decode(el_frame_decoder *decoder,
                              const void *data, size_t len, size_t *consumed,
                              el_frame *message);
```

with transports as `io` backends, and the rest left to each transport's own
API. Discovery should test whether a portable "IPC module" beyond framing
exists at all.

## 7. Sources

Revisions are the ones read for this milestone, on 2026-11-10. Statements
marked *source fact* above come from these files.

| Source | Revision | What was read |
|---|---|---|
| Kconfiglib fork, `github.com/zshoham/kconfiglib` | `242b8ff47f2a` (`main`) | `cmake/Kconfig*.cmake`, `genconfig.py`, `kconfiglib.py` (CMake output), `docs/guides/`, `tests/cmake/` |
| Zephyr, `github.com/zephyrproject-rtos/zephyr` | `291d9ddba6ff` (`main`; latest tag seen `v4.5.0`) | `doc/develop/api/api_lifecycle.rst`, `design_guidelines.rst`, `terminology.rst`; `include/zephyr/sys/sys_heap.h` |
| Pigweed, `pigweed.googlesource.com/pigweed/pigweed` | `3a747e3e3f3d` (`main`) | `docs/sphinx/facades.rst`; `pw_status/.../status.h`; `pw_allocator/.../allocator.h`, `deallocator.h`; `pw_stream/.../stream.h` |
| Zig standard library | 0.17.1, as installed | `std/mem/Allocator.zig`, `std/Io/Reader.zig`, `std/Io/Writer.zig`, `std/Io.zig` |
| TigerStyle, `github.com/tigerbeetle/tigerbeetle` | `68958eb75774` | `docs/TIGER_STYLE.md` |
| Sans-I/O, `sans-io.readthedocs.io` | undated; ©2016 | `how-to-sans-io.html` |

What each contributed, and what was left behind:

- **Zephyr.** Taken: Kconfig for configuration; the tiered API lifecycle and
  its "two implementations before it is stable" idea; tagging functions with
  the contexts they may be called from. Left: the platform model, devicetree,
  the kernel, and conditional struct members driven by configuration, which
  [ADR 0004](adr/0004-private-configuration.md) forbids in public headers.
- **Pigweed.** Taken: a small status vocabulary; its own advice to prefer
  dependency injection to facades. Left: its build tooling, the facade
  mechanism, and the full canonical code set.
- **Zig.** Taken: allocators and streams as explicit values; sized free; end
  of stream as a status. Left: buffers inside the interface, and anything
  that depends on the language.
- **TigerStyle.** Taken loosely: explicit limits, assertions on both sides of
  an interface, no allocation behind the caller's back. Left: the absolutes.
- **Sans-I/O.** Taken: the distinction in section 5, and the shape of the
  framing proposal.

Not verified:

- The rendered documentation sites of Zephyr and Pigweed. Their repository
  sources were read instead; `pigweed.dev` pages did not come through the
  fetch tool.
- Whether the Zig 0.17.1 interfaces are stable. They are cited as an example
  of a design, not as an API to track.
- Anything about the reference projects beyond the files listed.
