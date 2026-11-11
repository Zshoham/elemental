/*
 * elemental/io_posix.h - reader and writer over a POSIX file descriptor.
 *
 * STATUS: experimental. See docs/process/api-stability.md, and PORT.md next
 * to this backend for what it does and does not support.
 *
 * Each operation is exactly one read(2) or write(2) on the descriptor.
 *
 * Ownership: the caller opens the descriptor, keeps it open while the
 * el_posix_fd or any handle made from it is in use, and closes it. This
 * backend never closes, duplicates, or reconfigures a descriptor.
 *
 * Blocking: decided by the descriptor, which the caller owns. With
 * O_NONBLOCK set, an operation that cannot proceed returns
 * EL_ERR_WOULD_BLOCK. Without it, the operation waits in the kernel.
 *
 * Status mapping:
 *
 *   read() == 0                  EL_ERR_END_OF_STREAM
 *   EAGAIN, EWOULDBLOCK, EINTR   EL_ERR_WOULD_BLOCK (EINTR is not retried
 *                                here: the caller decides whether to)
 *   EPIPE on write               EL_ERR_END_OF_STREAM (the peer is gone)
 *   any other errno              EL_ERR_IO, errno kept for
 *                                el_posix_fd_last_errno()
 *
 * Signals: writing to a pipe or socket whose peer has closed raises SIGPIPE
 * before write() can return EPIPE. This backend does not change signal
 * dispositions, which are process-wide state. Ignore SIGPIPE in the
 * application to get EL_ERR_END_OF_STREAM instead of termination.
 *
 * Concurrency: no internal synchronization. One el_posix_fd may be used
 * from one thread at a time. Two el_posix_fd objects over the same
 * descriptor are independent as far as this backend is concerned.
 */
#ifndef ELEMENTAL_IO_POSIX_H
#define ELEMENTAL_IO_POSIX_H

#include <elemental/io.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Layout is public for allocation; use the functions, not the fields. */
typedef struct el_posix_fd {
    int fd;
    int last_errno;
} el_posix_fd;

/* Wrap `fd`, which must be a valid open descriptor (fd >= 0). */
void el_posix_fd_init(el_posix_fd *pfd, int fd);

/* Reader and writer handles for `pfd`. Both may be used on one object. */
el_reader el_posix_fd_as_reader(el_posix_fd *pfd);
el_writer el_posix_fd_as_writer(el_posix_fd *pfd);

/*
 * The errno behind the most recent EL_ERR_IO from this object, or 0 if it
 * has not returned one. Not cleared by later successful operations.
 */
int el_posix_fd_last_errno(const el_posix_fd *pfd);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_IO_POSIX_H */
