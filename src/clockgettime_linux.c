#include <unistd.h>
#include <time.h>
#include <errors.h>

#if defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0
#ifdef __linux__
#include <sys/syscall.h>
/* libc has incredibly messy way of doing this,
 * typically requiring -lrt. We just skip all this mess */
int clock_gettime(clockid_t clock_id, struct timespec *ts) {
    if(syscall(__NR_clock_gettime, clock_id, ts)) {
        return errno;
    } else {
        return 0;
    }
}
#endif
#endif

