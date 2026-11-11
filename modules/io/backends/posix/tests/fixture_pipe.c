/*
 * Contract fixtures for the POSIX backend, over pipes: once with blocking
 * descriptors and once with non-blocking ones.
 */
#define _POSIX_C_SOURCE 200809L

#include "fixture_pipe.h"

#include <elemental/io_posix.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct pipe_state {
    bool nonblocking;
    int read_fd;
    int write_fd;
    el_posix_fd stream;
} pipe_state;

static void fail(const char *what)
{
    perror(what);
    exit(2); /* The test could not be set up; nothing was verified. */
}

static void set_nonblocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        fail("fcntl");
    }
}

/* EAGAIN and EWOULDBLOCK may or may not be the same value. */
static bool is_no_progress(int error)
{
#if EAGAIN != EWOULDBLOCK
    if (error == EWOULDBLOCK) {
        return true;
    }
#endif
    return error == EAGAIN;
}

static void close_if_open(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static void reset(void *ctx)
{
    pipe_state *const state = ctx;
    close_if_open(&state->read_fd);
    close_if_open(&state->write_fd);
}

static void open_pipe(pipe_state *state)
{
    int fds[2];
    if (pipe(fds) != 0) {
        fail("pipe");
    }
    state->read_fd = fds[0];
    state->write_fd = fds[1];
}

static el_reader reader_with_content(void *ctx, const uint8_t *content, size_t len)
{
    pipe_state *const state = ctx;
    open_pipe(state);

    /* At most EL_IO_CONTRACT_SIZE_MAX bytes: fits in any pipe at once. */
    if (len > 0 && write(state->write_fd, content, len) != (ssize_t)len) {
        fail("write");
    }
    /* Closing the write end is what ends the stream for the reader. */
    close_if_open(&state->write_fd);

    if (state->nonblocking) {
        set_nonblocking(state->read_fd);
    }
    el_posix_fd_init(&state->stream, state->read_fd);
    return el_posix_fd_as_reader(&state->stream);
}

static el_writer writer_with_room(void *ctx, size_t room)
{
    pipe_state *const state = ctx;
    (void)room; /* A pipe holds far more than the suite writes. */
    open_pipe(state);

    /* The read end is the test's side; it must never wait in writer_take. */
    set_nonblocking(state->read_fd);
    if (state->nonblocking) {
        set_nonblocking(state->write_fd);
    }
    el_posix_fd_init(&state->stream, state->write_fd);
    return el_posix_fd_as_writer(&state->stream);
}

static size_t writer_take(void *ctx, uint8_t *out, size_t cap)
{
    pipe_state *const state = ctx;
    size_t total = 0;

    while (total < cap) {
        const ssize_t got = read(state->read_fd, out + total, cap - total);
        if (got <= 0) {
            if (got < 0 && !is_no_progress(errno)) {
                fail("read");
            }
            break;
        }
        total += (size_t)got;
    }
    return total;
}

/* Only a non-blocking descriptor can stall; a blocking one waits instead. */

static el_reader reader_stalled(void *ctx)
{
    pipe_state *const state = ctx;
    open_pipe(state); /* Empty, and the write end stays open. */
    set_nonblocking(state->read_fd);
    el_posix_fd_init(&state->stream, state->read_fd);
    return el_posix_fd_as_reader(&state->stream);
}

static el_writer writer_stalled(void *ctx)
{
    pipe_state *const state = ctx;
    open_pipe(state);
    set_nonblocking(state->write_fd);

    /* Fill the pipe: whole blocks first, then single bytes. */
    static const uint8_t block[4096];
    size_t size = sizeof(block);
    for (;;) {
        if (write(state->write_fd, block, size) > 0) {
            continue;
        }
        if (!is_no_progress(errno)) {
            fail("write");
        }
        if (size == 1) {
            break;
        }
        size = 1;
    }

    el_posix_fd_init(&state->stream, state->write_fd);
    return el_posix_fd_as_writer(&state->stream);
}

static pipe_state blocking = {.nonblocking = false, .read_fd = -1, .write_fd = -1};
static pipe_state nonblocking = {.nonblocking = true, .read_fd = -1, .write_fd = -1};

/* A pipe cannot be made to end after an exact byte count, so
 * writer_ending_after is absent; io_posix_test.cpp covers EPIPE. */

const el_io_contract_fixture io_posix_fixture_blocking_pipe = {
    .name = "io_posix (blocking pipe)",
    .ctx = &blocking,
    .reset = reset,
    .reader_with_content = reader_with_content,
    .writer_with_room = writer_with_room,
    .writer_take = writer_take,
};

const el_io_contract_fixture io_posix_fixture_nonblocking_pipe = {
    .name = "io_posix (non-blocking pipe)",
    .ctx = &nonblocking,
    .reset = reset,
    .reader_with_content = reader_with_content,
    .writer_with_room = writer_with_room,
    .writer_take = writer_take,
    .reader_stalled = reader_stalled,
    .writer_stalled = writer_stalled,
};
