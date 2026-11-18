# C contracts and interoperability

The rules a public Elemental interface follows. They exist so that a contract
reads the same way in every module, and so that it can be used through a C
ABI from C++, Rust, and Python.

Each rule is here once. Module headers apply them and document only what is
specific to the module. Where a rule is checked mechanically, the check is
named.

## Language baseline

**C11, without extensions**, for implementation and public headers
([ADR 0006](adr/0006-c11-and-naming.md)). Sources are compiled with
`-std=c11` and extensions off.

Public headers additionally:

- are valid **C++11**, inside `extern "C"`;
- are self-contained and safe to include twice;
- use no compiler extension unconditionally.

*Checked:* every public header is compiled alone, as C11 and as C++11, with
`-Wall -Wextra -Wpedantic -Werror` (`tests/headers`). New headers are picked
up automatically.

Sources that need a platform's feature-test macro define it at the top of
that file (`_POSIX_C_SOURCE` in `io_posix.c`), never on a command line.

## Naming

| Thing | Form | Example |
|---|---|---|
| Functions, types | `el_<module>_<noun>_<verb>`, lower case | `el_mem_reader_init` |
| Constants, macros | `EL_...`, upper case | `EL_OK`, `EL_ASSERT` |
| Status codes | `EL_OK`, `EL_ERR_<WHAT>` | `EL_ERR_WOULD_BLOCK` |
| Headers | `<elemental/<module>.h>` | `<elemental/io_mem.h>` |
| Kconfig symbols | unprefixed in Kconfig, `EL_CONFIG_<SYMBOL>` when generated | `EL_CONFIG_IO_POSIX` |
| CMake targets | `elemental::<module>` | `elemental::io_posix` |
| Operation tables | `<interface>_ops` | `el_reader_ops` |
| Handle from an object | `<type>_as_<interface>` | `el_mem_reader_as_reader` |

Types are `typedef`'d and used without `struct`. There is no `_t` suffix,
which POSIX reserves. The object a function acts on is its first parameter.
A context pointer is the first parameter of every callback and operation.
Units and qualifiers go last in a name (`timeout_ms`, `len_max`).

libedit also uses the `el_` prefix (`el_init`, `el_set`, ...), a known and
accepted overlap ([ADR 0006](adr/0006-c11-and-naming.md)). A new public
name must not equal one that libedit's `histedit.h` declares.

## Ownership and lifetime

Three words, used consistently:

- **Own**: responsible for ending it. Exactly one owner at a time.
- **Borrow**: may use it, for a stated period, and must not end it.
- **Handle**: a small value that borrows an object and is passed by value
  (`el_reader`, `el_writer`). A handle owns nothing.

The rules:

1. **Whoever creates something ends it.** Elemental does not take ownership
   of what it is handed. The POSIX backend never closes a descriptor.
2. **A borrow lasts for the call** unless the function says otherwise. A
   buffer passed to `el_write` is the caller's again when it returns; an
   implementation must copy what it keeps. *Checked* by the io contract suite.
3. **A longer borrow is stated with its end.** `el_mem_reader_init`
   borrows its bytes until the reader is no longer used.
4. **Interfaces have no destroy or close operation.** Code that is handed a
   handle cannot end the object behind it. Ending is done by the owner,
   through the concrete type.
5. **Destruction is explicit when there is something to release**, named
   `<type>_deinit`, and absent when there is not. Nothing in the slice holds
   a resource, so nothing has one. A type's header says which case applies.

## Storage, size, and alignment

1. **The caller provides the storage.** Object types have a public layout so
   they can live in static, stack, or heap memory. Initialization is
   `<type>_init(object, ...)` on storage the caller already has.
2. **Public layout is not public API.** Fields are visible so that `sizeof`
   works. Read and write them only through the type's functions.
3. **Objects may be copied or moved while no handle made from them is in
   use.** A handle holds the object's address. A type that cannot move even
   then must say so.
4. **Sizes and counts are `size_t`; alignments are a power of two in
   `size_t`.** A length never includes a terminator. Strings are passed with
   a length unless a parameter is documented as NUL-terminated.
5. **No function allocates** unless it takes an allocator
   ([architecture.md](architecture.md#5-resources-and-environmental-effects)).

## Errors

1. **A function that can fail at run time returns `el_status`** and
   delivers results through output parameters. A status and a value are
   never packed into one return (no "count or negative error").
2. **Exception:** a function whose result is a pointer and which has exactly
   one recoverable failure may return `NULL` for it. This is reserved for
   allocation, where the alternative is an invalid `void **` cast.
3. **A function that cannot fail returns its result or `void`.** It does not
   return a status that is always `EL_OK`.
4. **Output parameters are required and always written**, on every return
   path, including failures. What each holds for each status is documented.
   An output parameter is optional only where its header says so and the
   value is redundant on success. The one case today is the count of
   `el_read_exact` and `el_write_all`, which on `EL_OK` can only be
   the length that was asked for.
5. **Progress or status, never both**, for single-attempt operations
   ([ADR 0003](adr/0003-status-and-progress.md)). Operations that loop
   report both, and say so.
6. **Diagnostics beyond the code live with the object.** A backend keeps the
   platform's detail and exposes it through its own accessor
   (`el_posix_fd_last_errno`). There is no global or thread-local error
   state, and `errno` is not part of any contract.
7. **Statuses are for callers; assertions are for bugs.** See below.
8. **A status must be used.** Every function whose result reports a
   failure is declared `EL_NODISCARD`: each one that returns `el_status`,
   and an allocation that returns `NULL`. A caller acts on the status,
   passes it on, or discards it on purpose with `el_status_ignore()`; a
   `(void)` cast does not satisfy every compiler. A status that cannot be
   anything but `EL_OK` where the call is made is asserted, not ignored
   ([ADR 0018](adr/0018-must-use-and-propagation.md)).
9. **Passing a status on is spelled `EL_TRY(expr)`**, or
   `EL_TRY_GOTO(expr, status, label)` in a function that has something to
   release. A failure a function finds itself, in data it was given, is
   `EL_ENSURE(condition, status)`. Each expands to the `if` it replaces,
   which stays correct, and is what code that compares against a
   particular status writes. A function that owns a resource calls one that
   does the work, so that the work can leave from anywhere.
10. **Negative statuses are the application's.** Elemental defines none and
    never returns one of its own; a function in front of an interface
    returns one unchanged when an operation the application implemented
    produced it. A backend in this repository returns Elemental's codes
    only ([ADR 0017](adr/0017-negative-statuses.md)).

*Checked:* `headers.nodiscard` reads every header under `modules/` and
`examples/` for a function that returns `el_status` without
`EL_NODISCARD`. `status.nodiscard_check.*` compiles a call that drops a
status and expects the compiler's diagnostic, as C11, C++17, and C23.
`io.helpers` and `io.unit.buffered` show a negative status passing through
unchanged.

## Arguments

| Case | Rule |
|---|---|
| Null object or handle | Contract violation. |
| Null output parameter | Contract violation, unless documented as optional. |
| Null buffer with size 0 | Valid everywhere. It is how "nothing" is passed. |
| Null buffer with nonzero size | Contract violation. |
| Size 0 | Valid, and a no-op that succeeds, unless documented otherwise. The implementation behind an interface is not consulted. |
| Arithmetic on sizes | Must not overflow. A function that multiplies or adds caller-supplied sizes checks first and reports failure as a status, because the values may come from input. |
| A value out of its documented range | Contract violation if only a program bug can produce it. A status if input data can. |

The last row is the dividing line for the whole document: **can a correct
program reach this with bad input?** If yes, it is a status. If only a bug
can, it is an assertion. The two have sibling spellings:
`EL_ENSURE(condition, status)` and `EL_ASSERT(condition)`.

## Assertions and recoverable failures

`EL_ASSERT(condition)` states something true in every correct program. It
is used on both sides of an interface:

- **preconditions**, which a caller must meet;
- **postconditions of an implementation**, checked by the public function
  that called it. A backend that returns success with no progress is caught
  there, not in its caller.

What a failed assertion does is configuration
([modules/status/Kconfig](../modules/status/Kconfig)): trap in place by
default, call the application's `el_assert_failed()`, or, with assertions
compiled out, nothing. In that last configuration **violating a documented
precondition is undefined behavior.** Assertions are never the only thing
standing between input data and memory safety.

*Checked:* `modules/io/tests/io_violations_test.cpp` verifies, with death
tests, that each documented violation stops the program, for callers and
for implementations, in both assertion-handler configurations.

An assertion condition must have no side effect the program depends on. It
is still compiled when assertions are off, so it cannot go stale.

## Concurrency and execution context

Every public header states these four things. The defaults, when a header is
silent, are the conservative ones.

| Property | Default | How a module says more |
|---|---|---|
| **Thread safety** | None. One object, one context at a time. Distinct objects are independent. Constants and pure functions are always safe. | "Safe to call concurrently on one object", with the conditions. |
| **Reentrancy** | A function may be called again on a different object from inside a callback. Not on the same object. | Stated per callback. |
| **Interrupt handlers** | Not allowed. | "ISR-safe", meaning: does not block, does not allocate, takes no lock, and is safe under the one-context rule. |
| **Blocking** | Does not block. | A function that may wait says what it waits for. For interfaces, blocking is a property of the implementation, and each one documents it. |

No Elemental module contains a lock or thread-local state. One contains
atomics: [`ring`](../modules/ring/README.md), whose object is used by two
contexts at once, and the deferred log sink built on it. The only atomic
operations they use are a load and a store of one word, each word written
by one context
([ADR 0023](adr/0023-atomics-and-interrupt-context.md)). A module that
needs more than that takes it as an explicit decision in its proposal.

## ABI

**Source portability and binary compatibility are different promises.**

*Source portability* is what Elemental aims at: the same source compiles and
behaves the same with any conforming C11 compiler on any target. It is
verified for the compilers and targets in
[build.md](build.md#what-is-verified) and claimed for no others.

*Binary compatibility* is not promised across anything. A compiled Elemental
library and the code that links it must be built for the same target, by
ABI-compatible compilers, from the same Elemental version. Specifically:

| Across | Guarantee |
|---|---|
| Compilers | None beyond what the platform's C ABI gives. |
| Architectures and data models | None. `size_t` and pointers differ, so layouts do. |
| Elemental versions | None before 1.0 ([api-stability.md](process/api-stability.md)). |
| **Configurations** | Public types and function signatures are the same in every configuration, by rule ([ADR 0004](adr/0004-private-configuration.md)). Behavior differs. |

The rules that keep a contract usable through a C ABI:

1. **No enum types in signatures or structs.** The compiler chooses an enum's
   size, and `-fshort-enums` changes it. Constants are declared in an
   anonymous `enum`; the type that carries them is a fixed-width integer
   (`typedef int32_t el_status`).
2. **Fixed-width integers for values with a defined range**; `size_t` for
   sizes; `int` only where the language or a platform API dictates it (a
   line number, a file descriptor). No `long`, no bit-fields, no `bool` in a
   public struct.
3. **No public layout depends on configuration.** No `#ifdef` inside a public
   struct.
4. **Operation tables only grow at a breaking release**, and only at the end.
   They are initialized with designated initializers, so an added optional
   operation is null in existing code.
5. **No variadic functions in a contract.** A variadic C convenience may sit
   on top of a non-variadic core, in a separate header.
6. **Macros are for constants and for things only a macro can do**
   (`EL_ASSERT` needs the call site's file and line; `EL_TRY` leaves the
   function it is written in). An interface is never only reachable
   through a macro.
7. **Nothing unwinds through Elemental.** Callbacks and operations must not
   throw, panic across the boundary, or `longjmp` out. The one exception is
   the assertion handler, which never returns to its caller by definition.
8. **Structs are passed by value only when they are handles** (two
   pointers) **or layouts** (`el_layout`, two `size_t`): at most two words,
   trivially copyable, with no `bool`, enum, or bit-field. Such a struct is
   passed in registers on the AAPCS and on System V x86-64
   ([ADR 0013](adr/0013-allocation-layout.md)).

## Other languages

The contracts are meant to be implementable and usable from C++ and Rust and
exposable through Python extension modules. Elemental ships no bindings. The
direction is validated by three small programs in
[tests/interop](../tests/interop), each of which exercises ownership and a
callback in both directions:

| Language | File | Verification level |
|---|---|---|
| C++11 | `cxx_smoke.cpp` | Compiled, linked, run (`ctest -R interop.cxx`) |
| Rust | `rust_sketch.rs` | Compiled, linked against the C libraries, run (`ctest -R interop.rust`) |
| Python | `python_ext_sketch.c` | **Compile check only**, against the CPython headers. Never linked or imported. |

What they establish, and the rule each depends on:

**Ownership.** Caller-owned storage maps onto each language's native
lifetime: a C++ object's members, a Rust struct with a lifetime parameter
for the borrowed bytes, a Python object that pins a buffer until it is
deallocated. Because handles are made per call and not stored, the Rust
wrapper needs no pinning or boxing: the "movable while no handle is in use"
rule is exactly what Rust's borrow checker can enforce. Because Elemental
never allocates, there is no allocator to agree on across the boundary.

**Callbacks.** An operation implemented in another language is a plain C
function with a context pointer. The rule that nothing unwinds is met
differently in each language, and each sketch shows the pattern:

- C++: an `extern "C"`, `noexcept` function catches everything, stores the
  `std::exception_ptr` in its object, and returns `EL_ERR_IO`. The caller
  rethrows after the C call returns.
- Rust: `catch_unwind` in the trampoline; an `io::Error` is kept in the
  adapter and returned by the Rust wrapper, so the caller gets the original
  error and not just a status.
- Python: the exception is left set, the operation returns `EL_ERR_IO`,
  and the extension function returns `NULL` so the original exception
  propagates. A Python exception is state, not unwinding.

**Status mapping.** "Progress or status, never both" makes the mapping to
`std::io::Result<usize>` and to Python's `read()` total: there is no case
where a count and an error must both be returned.

Friction found, and what was done about it:

- A C++ member function cannot be given C linkage. Operations are free
  functions; this is a rule for implementers, not a defect.
- Linking Elemental into a Python extension needs position-independent
  code. That is the consumer's build setting
  (`CMAKE_POSITION_INDEPENDENT_CODE=ON`), not an Elemental option, and it is
  why the Python sketch stops at a compile check.
- A binding that releases Python's GIL around a blocking Elemental call must
  reacquire it inside operations implemented in Python. The sketch does not
  release it.

Not established: anything about completion-based or asynchronous contracts,
where a buffer outlives the call. See
[architecture.md](architecture.md#65-async-and-ipc--discovery-before-design).
