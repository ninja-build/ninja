#ifndef __WIN32_PORT_H__
#define __WIN32_PORT_H__

#include <direct.h>
#include <assert.h>
#include <time.h>
#include <io.h>
#include <sys/timeb.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#define mkdir(path, mod) _mkdir(((mod),(path)))
#define getcwd _getcwd
#define isatty _isatty
#define snprintf _snprintf

#pragma warning(disable : 4996) // deprecate function use
#define setlinebuf(f) do { int err = setvbuf(f, 0, _IOLBF, 4096); assert(err == 0); } while (0)
#define PATH_MAX 4096

struct timeval {
        long    tv_sec;         /* seconds */
        long    tv_usec;        /* and microseconds */
};
inline void gettimeofday(timeval* t, void*)
{
  _timeb time;
  _ftime(&time);
  t->tv_sec = (long) time.time;
  t->tv_usec = time.millitm*1000;
}

inline void timersub(const timeval* a, const timeval* b, timeval* result)             
{
  do {                                                                        \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                             \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                          \
    if ((result)->tv_usec < 0) {                                              \
      --(result)->tv_sec;                                                     \
      (result)->tv_usec += 1000000;                                           \
    }                                                                         \
  } while (0);
}

#endif // __WIN32_PORT_H__
