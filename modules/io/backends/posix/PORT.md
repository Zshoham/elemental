# Port: io on POSIX file descriptors

This document follows the template in
[docs/process/porting.md](../../../../docs/process/porting.md).

| | |
|---|---|
| Interface | `el_reader`, `el_writer` ([io.h](../../include/elemental/io.h)) |
| Platform facility | `read(2)`, `write(2)` on a descriptor the caller owns |
| Target | `elemental::io_posix` |
| Header | [`elemental/io_posix.h`](include/elemental/io_posix.h) |
| Stability | Experimental |
| Verification level | **Tested on a host**: Linux x86-64 only |

## Capabilities

| Interface behavior | Supported | Notes |
|---|---|---|
| Partial reads and writes | Yes | Whatever the kernel returns. |
| End of stream, reader | Yes | `read()` returning 0. |
| End of stream, writer | Yes | `EPIPE`. Requires `SIGPIPE` to be ignored; see below. |
| Would-block | Yes | When the caller set `O_NONBLOCK`. Also returned for `EINTR`. |
| Blocking operation | Yes | When the caller did not set `O_NONBLOCK`. |
| Failure detail | Yes | `el_posix_fd_last_errno()`. |
| Reader and writer on one object | Yes | For sockets and other bidirectional descriptors. |

## Unsupported semantics

- **Does not ignore `SIGPIPE`.** Signal dispositions are process-wide state,
  which a backend must not change. Writing to a closed pipe or socket kills
  the process unless the application ignores the signal. Sockets could avoid
  this with `send(..., MSG_NOSIGNAL)`; this backend uses `write()` for every
  descriptor type and does not.
- **Does not retry `EINTR`.** It is reported as `EL_ERR_WOULD_BLOCK`. A
  caller looping on would-block over a *blocking* descriptor therefore spins
  for as long as signals keep arriving, and should wait instead.
- **Does not wait.** There is no poll or select here. Waiting for a
  non-blocking descriptor is the application's event loop.
- **Does not open, close, duplicate, or configure descriptors.**
- **No end of stream after a byte count.** A descriptor ends when the kernel
  says so.

## Toolchain and runtime dependencies

A hosted C11 toolchain with `<unistd.h>`, on Linux, the only supported
host operating system. Uses `read`, `write`, and `errno`,
and defines `_POSIX_C_SOURCE 200809L` in its own source file. No threads, no
allocation.

## Configuration

`IO_POSIX`, on by default where CMake finds `<unistd.h>` (`PLATFORM_POSIX`).
Selecting it for a toolchain without that header is a configuration error.

## Concurrency and context

No internal synchronization: one `el_posix_fd` from one thread at a time.
Not for interrupt handlers. Blocks exactly when the descriptor does.

## Verification

| Test | Kind | What it checks |
|---|---|---|
| `io_posix.contract.*` | Shared contract suite | Interface semantics, over pipes, once blocking and once non-blocking. |
| `io_posix.platform.*` | Backend-specific | `EPIPE` is end of stream; an OS failure is `EL_ERR_IO` and keeps its `errno`; the descriptor stays open and its flags untouched; one object reads and writes a socket pair; an interrupted blocking read is would-block. |

Skipped contract tests: 5 over the blocking pipe and 3 over the non-blocking
one. A blocking pipe cannot be made to stall (2 tests); a pipe cannot end,
or be full without stalling, after an exact number of bytes are written (2
tests, both fixtures); and its reader ends after the content, so the check
of a reader that stalls after it is skipped (1 test, both fixtures).

Run with GCC 16.2.1 and Clang 23.1.4, and under AddressSanitizer and
UndefinedBehaviorSanitizer, on Linux x86-64.

Linux is the only supported operating system; others are deferred
([roadmap](../../../../docs/roadmap.md#deferred-deliberately)). The backend
uses only POSIX calls, but nothing is claimed for another system.

Not verified: descriptor types other than pipes and Unix sockets, and
behavior under real signal load.
