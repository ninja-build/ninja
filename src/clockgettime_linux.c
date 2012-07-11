<<<<<<< HEAD
// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <unistd.h>
#include <time.h>
#include <errno.h>

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

