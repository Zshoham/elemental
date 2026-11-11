/*
 * Platform behavior of the POSIX backend: what elemental/io_posix.h
 * promises beyond the io interface contract.
 */
#include <gtest/gtest.h>

#include <elemental/io_posix.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <string>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace
{

constexpr size_t kUnset = static_cast<size_t>(-1);

class IoPosix : public testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        /* Otherwise the first write to a broken pipe ends the process; see
         * elemental/io_posix.h. */
        signal(SIGPIPE, SIG_IGN);
    }

    void TearDown() override
    {
        for (int &fd : fds) {
            if (fd >= 0) {
                close(fd);
                fd = -1;
            }
        }
    }

    void OpenPipe()
    {
        ASSERT_EQ(pipe(fds), 0);
    }

    void OpenSocketPair()
    {
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    }

    void Close(int index)
    {
        ASSERT_EQ(close(fds[index]), 0);
        fds[index] = -1;
    }

    /* A pipe: [0] is the read end, [1] the write end. */
    int fds[2] = {-1, -1};
    el_posix_fd stream;
};

TEST_F(IoPosix, BrokenPipeIsEndOfStream)
{
    ASSERT_NO_FATAL_FAILURE(OpenPipe());
    ASSERT_NO_FATAL_FAILURE(Close(0)); /* The peer goes away. */

    el_posix_fd_init(&stream, fds[1]);
    const el_writer writer = el_posix_fd_as_writer(&stream);

    /* Permanent, and not an environment failure. */
    for (int i = 0; i < 2; i++) {
        size_t n = kUnset;
        EXPECT_EQ(el_write(writer, "x", 1, &n), EL_ERR_END_OF_STREAM);
        EXPECT_EQ(n, 0u);
    }
    EXPECT_EQ(el_posix_fd_last_errno(&stream), 0);
}

TEST_F(IoPosix, OsFailureIsIoAndKeepsErrno)
{
    ASSERT_NO_FATAL_FAILURE(OpenPipe());

    /* Reading from the write end of a pipe fails with EBADF. */
    el_posix_fd_init(&stream, fds[1]);
    EXPECT_EQ(el_posix_fd_last_errno(&stream), 0);

    uint8_t byte;
    size_t n = kUnset;
    EXPECT_EQ(el_read(el_posix_fd_as_reader(&stream), &byte, 1, &n), EL_ERR_IO);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(el_posix_fd_last_errno(&stream), EBADF);

    /* A later success does not erase the record of the failure. */
    n = kUnset;
    EXPECT_EQ(el_write(el_posix_fd_as_writer(&stream), "x", 1, &n), EL_OK);
    EXPECT_EQ(n, 1u);
    EXPECT_EQ(el_posix_fd_last_errno(&stream), EBADF);
}

TEST_F(IoPosix, DescriptorStaysOpenAndOwnedByTheCaller)
{
    ASSERT_NO_FATAL_FAILURE(OpenPipe());

    el_posix_fd_init(&stream, fds[1]);
    size_t n = kUnset;
    EXPECT_EQ(el_write_all(el_posix_fd_as_writer(&stream), "abc", 3, &n), EL_OK);

    /* Still open, flags untouched: the backend only reads and writes. */
    EXPECT_NE(fcntl(fds[1], F_GETFD), -1);
    EXPECT_EQ(fcntl(fds[1], F_GETFL) & O_NONBLOCK, 0);

    /* Closing is the caller's, and still works. */
    Close(0);
    Close(1);
}

TEST_F(IoPosix, OneObjectReadsAndWrites)
{
    ASSERT_NO_FATAL_FAILURE(OpenSocketPair());

    el_posix_fd_init(&stream, fds[0]);
    const el_reader reader = el_posix_fd_as_reader(&stream);
    const el_writer writer = el_posix_fd_as_writer(&stream);

    size_t n = kUnset;
    EXPECT_EQ(el_write_all(writer, "ping", 4, &n), EL_OK);
    char peer_buf[4];
    ASSERT_EQ(read(fds[1], peer_buf, 4), 4);
    EXPECT_EQ(std::string(peer_buf, 4), "ping");

    ASSERT_EQ(write(fds[1], "pong", 4), 4);
    char buf[4];
    n = kUnset;
    EXPECT_EQ(el_read_exact(reader, buf, 4, &n), EL_OK);
    EXPECT_EQ(std::string(buf, 4), "pong");
}

extern "C" void OnAlarm(int)
{
}

TEST_F(IoPosix, InterruptedCallIsWouldBlock)
{
    ASSERT_NO_FATAL_FAILURE(OpenPipe());

    /* A blocking read on an empty pipe, interrupted by a signal whose
     * handler does not restart system calls. */
    struct sigaction action = {};
    action.sa_handler = OnAlarm;
    ASSERT_EQ(sigaction(SIGALRM, &action, nullptr), 0);

    itimerval once = {};
    once.it_value.tv_usec = 20000;
    ASSERT_EQ(setitimer(ITIMER_REAL, &once, nullptr), 0);

    el_posix_fd_init(&stream, fds[0]);
    uint8_t byte;
    size_t n = kUnset;
    EXPECT_EQ(el_read(el_posix_fd_as_reader(&stream), &byte, 1, &n), EL_ERR_WOULD_BLOCK);
    EXPECT_EQ(n, 0u);
    /* Not an environment failure, so nothing is recorded. */
    EXPECT_EQ(el_posix_fd_last_errno(&stream), 0);
}

} // namespace
