/*
 * Consumer example: copy standard input to standard output.
 *
 * copy_stream() is written against the io interfaces only. It handles the
 * three things a stream can do besides deliver bytes: end, stall, and fail.
 */
#include <elemental/io.h>
#include <elemental/io_posix.h>
#include <elemental/status.h>

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Copy until `in` ends. Returns EL_OK when everything was copied, or the
 * status that stopped the copy. Never waits on its own: if a stream stalls,
 * the caller is told and decides what waiting means.
 */
static el_status copy_stream(el_reader in, el_writer out, size_t *copied)
{
    char chunk[4096];
    *copied = 0;

    for (;;) {
        size_t got;
        el_status status = el_read(in, chunk, sizeof(chunk), &got);
        if (status == EL_ERR_END_OF_STREAM) {
            return EL_OK;
        }
        if (status != EL_OK) {
            return status;
        }

        size_t put;
        status = el_write_all(out, chunk, got, &put);
        *copied += put;
        if (status != EL_OK) {
            return status;
        }
    }
}

int main(void)
{
    /* So that a closed output is reported as a status, not a signal. */
    signal(SIGPIPE, SIG_IGN);

    /* The application owns the descriptors; the backend only uses them. */
    el_posix_fd in;
    el_posix_fd out;
    el_posix_fd_init(&in, STDIN_FILENO);
    el_posix_fd_init(&out, STDOUT_FILENO);

    size_t copied;
    const el_status status =
        copy_stream(el_posix_fd_as_reader(&in), el_posix_fd_as_writer(&out), &copied);

    if (status == EL_ERR_IO) {
        /* The detail lives with the object that failed. */
        const int error = el_posix_fd_last_errno(&in) != 0 ? el_posix_fd_last_errno(&in)
                                                           : el_posix_fd_last_errno(&out);
        fprintf(stderr, "el_cat: %s\n", strerror(error));
        return 1;
    }
    if (status != EL_OK) {
        /* Blocking descriptors do not stall, so this is a closed output or
         * an interrupted call. */
        fprintf(stderr, "el_cat: stopped after %zu bytes: %s\n", copied, el_status_name(status));
        return 1;
    }
    return 0;
}
