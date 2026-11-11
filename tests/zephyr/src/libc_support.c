/*
 * Two functions GoogleTest and libstdc++ call that picolibc on a Zephyr
 * Cortex-M board does not provide. native_sim does not need them.
 */
#include <zephyr/kernel.h>

#include <errno.h>
#include <sys/time.h>
#include <unistd.h>

/* Whether to print in colour. The console is not a terminal. */
int isatty(int fd)
{
    (void)fd;
    errno = ENOTTY;
    return 0;
}

/*
 * GoogleTest times each test with std::chrono::system_clock, which libstdc++
 * reads with gettimeofday(). Time since boot is enough for that. Zephyr's
 * own gettimeofday() (CONFIG_XSI_SINGLE_PROCESS) needs a hardware info
 * driver these boards do not have.
 */
int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    int64_t ms = k_uptime_get();
    tv->tv_sec = (time_t)(ms / 1000);
    tv->tv_usec = (suseconds_t)((ms % 1000) * 1000);
    return 0;
}
