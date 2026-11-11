/* read(2), write(2), ssize_t and SSIZE_MAX, under -std=c11 without extensions. */
#define _POSIX_C_SOURCE 200809L

#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/io_posix.h>

#include <errno.h>
#include <limits.h>
#include <unistd.h>

/* read() and write() with a count above SSIZE_MAX are implementation-defined. */
static size_t clamp_count(size_t count)
{
    return count > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : count;
}

static int is_would_block(int error)
{
/* POSIX lets these two be one value, and comparing against both would then
 * be a duplicate condition. */
#if EAGAIN != EWOULDBLOCK
    if (error == EWOULDBLOCK) {
        return 1;
    }
#endif
    return error == EAGAIN || error == EINTR;
}

static el_status fd_read(void *ctx, void *buf, size_t cap, size_t *n)
{
    el_posix_fd *const pfd = ctx;

    const ssize_t result = read(pfd->fd, buf, clamp_count(cap));
    if (result > 0) {
        *n = (size_t)result;
        return EL_OK;
    }
    if (result == 0) {
        return EL_ERR_END_OF_STREAM;
    }

    const int error = errno;
    if (is_would_block(error)) {
        return EL_ERR_WOULD_BLOCK;
    }
    pfd->last_errno = error;
    return EL_ERR_IO;
}

static el_status fd_write(void *ctx, const void *data, size_t len, size_t *n)
{
    el_posix_fd *const pfd = ctx;

    const ssize_t result = write(pfd->fd, data, clamp_count(len));
    if (result > 0) {
        *n = (size_t)result;
        return EL_OK;
    }
    if (result == 0) {
        /* Nothing accepted and no error: the same as "not right now". */
        return EL_ERR_WOULD_BLOCK;
    }

    const int error = errno;
    if (is_would_block(error)) {
        return EL_ERR_WOULD_BLOCK;
    }
    if (error == EPIPE) {
        return EL_ERR_END_OF_STREAM;
    }
    pfd->last_errno = error;
    return EL_ERR_IO;
}

static const el_reader_ops fd_reader_ops = {
    .read = fd_read,
};

static const el_writer_ops fd_writer_ops = {
    .write = fd_write,
};

void el_posix_fd_init(el_posix_fd *pfd, int fd)
{
    EL_ASSERT(pfd != NULL);
    EL_ASSERT(fd >= 0);

    pfd->fd = fd;
    pfd->last_errno = 0;
}

el_reader el_posix_fd_as_reader(el_posix_fd *pfd)
{
    EL_ASSERT(pfd != NULL);

    const el_reader reader = {.ctx = pfd, .ops = &fd_reader_ops};
    return reader;
}

el_writer el_posix_fd_as_writer(el_posix_fd *pfd)
{
    EL_ASSERT(pfd != NULL);

    const el_writer writer = {.ctx = pfd, .ops = &fd_writer_ops};
    return writer;
}

int el_posix_fd_last_errno(const el_posix_fd *pfd)
{
    EL_ASSERT(pfd != NULL);

    return pfd->last_errno;
}
