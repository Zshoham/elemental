/*
 * Consumer example: copy standard input to standard output.
 *
 * copy_stream() is written against the io interfaces only. It handles the
 * three things a stream can do besides deliver bytes: end, stall, and fail.
 * What goes wrong is logged: main() decides that log records are lines on
 * standard error, timed by the system's clock.
 */
#include <elemental/io.h>
#include <elemental/io_posix.h>
#include <elemental/log.h>
#include <elemental/log_printf.h>
#include <elemental/log_writer.h>
#include <elemental/status.h>
#include <elemental/time_posix.h>

#include <signal.h>
#include <string.h>
#include <unistd.h>

/*
 * Copy until `in` ends. Returns EL_OK when everything was copied, or the
 * status that stopped the copy. Never waits on its own: if a stream stalls,
 * the caller is told and decides what waiting means.
 */
EL_NODISCARD static el_status copy_stream(el_reader in, el_writer out, size_t *copied)
{
    char chunk[4096];
    *copied = 0;

    for (;;) {
        size_t got;
        el_status status = el_read(in, chunk, sizeof(chunk), &got);
        if (status == EL_ERR_END_OF_STREAM) {
            return EL_OK; /* the one status that is not a failure here */
        }
        EL_TRY(status);

        size_t put;
        status = el_write_all(out, chunk, got, &put);
        *copied += put;
        EL_TRY(status);
    }
}

int main(void)
{
    /* So that a closed output is reported as a status, not a signal. */
    signal(SIGPIPE, SIG_IGN);

    /* The application owns the descriptors; the backend only uses them. */
    el_posix_fd in;
    el_posix_fd out;
    el_posix_fd err;
    el_posix_fd_init(&in, STDIN_FILENO);
    el_posix_fd_init(&out, STDOUT_FILENO);
    el_posix_fd_init(&err, STDERR_FILENO);

    /* Where log records go, how one becomes a line, and whose they are:
     * each is the application's to decide, in storage it provides. */
    char line_storage[160];
    el_buf line;
    el_buf_init(&line, line_storage, sizeof(line_storage));
    el_log_writer_sink sink;
    el_log_writer_sink_init(&sink, el_posix_fd_as_writer(&err), &line);
    el_logger log;
    el_logger_init(&log, el_log_writer_sink_as_sink(&sink), el_posix_clock(), "el_cat",
                   EL_LOG_LEVEL_INFO);

    size_t copied;
    const el_status status =
        copy_stream(el_posix_fd_as_reader(&in), el_posix_fd_as_writer(&out), &copied);

    if (status == EL_ERR_IO) {
        /* The detail lives with the object that failed. */
        const int error = el_posix_fd_last_errno(&in) != 0 ? el_posix_fd_last_errno(&in)
                                                           : el_posix_fd_last_errno(&out);
        EL_LOG_ERROR(&log, "%s", strerror(error));
        return 1;
    }
    if (status != EL_OK) {
        /* Blocking descriptors do not stall, so this is a closed output or
         * an interrupted call. */
        EL_LOG_ERROR(&log, "stopped after %zu bytes: %s", copied, el_status_name(status));
        return 1;
    }
    /* Below this logger's level: nothing is formatted and nothing written. */
    EL_LOG_DEBUG(&log, "copied %zu bytes", copied);
    return 0;
}
