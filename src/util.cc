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

#include "util.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef _WIN32
#include <sys/time.h>
#endif

#include <vector>

#ifdef _WIN32
#include <direct.h>  // _mkdir
#endif

#include "edit_distance.h"
#include "metrics.h"

void Fatal(const char* msg, ...) {
  va_list ap;
  fprintf(stderr, "ninja: FATAL: ");
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fprintf(stderr, "\n");
#ifdef _WIN32
  //__debugbreak();
#endif
  exit(1);
}

void Warning(const char* msg, ...) {
  va_list ap;
  fprintf(stderr, "ninja: WARNING: ");
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fprintf(stderr, "\n");
}

void Error(const char* msg, ...) {
  va_list ap;
  fprintf(stderr, "ninja: ERROR: ");
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fprintf(stderr, "\n");
}

bool CanonicalizePath(string* path, string* err) {
  METRIC_RECORD("canonicalize str");
  int len = path->size();
  char* str = 0;
  if (len > 0)
    str = &(*path)[0];
  if (!CanonicalizePath(str, &len, err))
    return false;
  path->resize(len);
  return true;
}

bool CanonicalizePath(char* path, int* len, string* err) {
  // WARNING: this function is performance-critical; please benchmark
  // any changes you make to it.
  METRIC_RECORD("canonicalize path");
  if (*len == 0) {
    *err = "empty path";
    return false;
  }

  const int kMaxPathComponents = 30;
  char* components[kMaxPathComponents];
  int component_count = 0;

  char* start = path;
  char* dst = start;
  const char* src = start;
  const char* end = start + *len;

  if (*src == DIR_SEP[0]) {
    ++src;
    ++dst;
  }

  while (src < end) {
    const char* sep = (const char*)memchr(src, DIR_SEP[0], end - src);
    if (sep == NULL)
      sep = end;

    if (*src == '.') {
      if (sep - src == 1) {
        // '.' component; eliminate.
        src += 2;
        continue;
      } else if (sep - src == 2 && src[1] == '.') {
        // '..' component.  Back up if possible.
        if (component_count > 0) {
          dst = components[component_count - 1];
          src += 3;
          --component_count;
        } else {
          while (src <= sep)
            *dst++ = *src++;
        }
        continue;
      }
    }

    if (sep > src) {
      if (component_count == kMaxPathComponents)
        Fatal("path has too many components");
      components[component_count] = dst;
      ++component_count;
      while (src <= sep) {
        *dst++ = *src++;
      }
    }

    src = sep + 1;
  }

  if (dst == start) {
    *err = "path canonicalizes to the empty path";
    return false;
  }

  *len = dst - start - 1;
  return true;
}

int MakeDir(const string& path) {
#ifdef _WIN32
  return _mkdir(path.c_str());
#else
  return mkdir(path.c_str(), 0777);
#endif
}

int ReadFile(const string& path, string* contents, string* err, bool binary) {
  FILE* f = fopen(path.c_str(), binary ? "rb" : "r");
  if (!f) {
    err->assign(strerror(errno));
    return -errno;
  }
  if (!ReadFile(f, contents, err))
    return -errno;
  fclose(f);
  return 0;
}

bool ReadFile(FILE* f, string* contents, string* err) {
  char buf[64 << 10];
  size_t len;
  while ((len = fread(buf, 1, sizeof(buf), f)) > 0) {
    contents->append(buf, len);
  }
  if (ferror(f)) {
    err->assign(strerror(errno));  // XXX errno?
    contents->clear();
    return false;
  }
  return true;
}

void SetCloseOnExec(int fd) {
#ifndef _WIN32
  int flags = fcntl(fd, F_GETFD);
  if (flags < 0) {
    perror("fcntl(F_GETFD)");
  } else {
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
      perror("fcntl(F_SETFD)");
  }
#else
  HANDLE hd = (HANDLE) _get_osfhandle(fd);
  if (! SetHandleInformation(hd, HANDLE_FLAG_INHERIT, 0)) {
    fprintf(stderr, "SetHandleInformation(): %s", GetLastErrorString().c_str());
  }
#endif  // ! _WIN32
}

int64_t GetTimeMillis() {
#ifdef _WIN32
  // GetTickCount64 is only available on Vista or later.
  return GetTickCount();
#else
  timeval now;
  gettimeofday(&now, NULL);
  return ((int64_t)now.tv_sec * 1000) + (now.tv_usec / 1000);
#endif
}

const char* SpellcheckStringV(const string& text,
                              const vector<const char*>& words) {
  const bool kAllowReplacements = true;
  const int kMaxValidEditDistance = 3;

  int min_distance = kMaxValidEditDistance + 1;
  const char* result = NULL;
  for (vector<const char*>::const_iterator i = words.begin();
       i != words.end(); ++i) {
    int distance = EditDistance(*i, text, kAllowReplacements,
                                kMaxValidEditDistance);
    if (distance < min_distance) {
      min_distance = distance;
      result = *i;
    }
  }
  return result;
}

const char* SpellcheckString(const string& text, ...) {
  va_list ap;
  va_start(ap, text);
  vector<const char*> words;
  const char* word;
  while ((word = va_arg(ap, const char*)))
    words.push_back(word);
  return SpellcheckStringV(text, words);
}

#ifdef _WIN32
string GetLastErrorString() {
  DWORD err = GetLastError();

  char* msg_buf;
  FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (char*)&msg_buf,
        0,
        NULL);
  string msg = msg_buf;
  LocalFree(msg_buf);
  return msg;
}
#endif

static bool islatinalpha(int c) {
  // isalpha() is locale-dependent.
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

string StripAnsiEscapeCodes(const string& in) {
  string stripped;
  stripped.reserve(in.size());

  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] != '\33') {
      // Not an escape code.
      stripped.push_back(in[i]);
      continue;
    }

    // Only strip CSIs for now.
    if (i + 1 >= in.size()) break;
    if (in[i + 1] != '[') continue;  // Not a CSI.
    i += 2;

    // Skip everything up to and including the next [a-zA-Z].
    while (i < in.size() && !islatinalpha(in[i]))
      ++i;
  }
  return stripped;
}

TimeStamp FiletimeToTimestamp(const FILETIME& filetime) {
  // FILETIME is in 100-nanosecond increments since the Windows epoch.
  // We don't much care about epoch correctness but we do want the
  // resulting value to fit in an integer.
  uint64_t mtime = ((uint64_t)filetime.dwHighDateTime << 32) |
    ((uint64_t)filetime.dwLowDateTime);
  mtime /= 1000000000LL / 100; // 100ns -> s.
  mtime -= 12622770400LL;  // 1600 epoch -> 2000 epoch (subtract 400 years).
  return (TimeStamp)mtime;

#ifdef _WIN32
static double GetLoadAverage_win32()
{
  // TODO(nicolas.despres@gmail.com): Find a way to implement it on Windows.
  return -0.0f;
}
#else
static double GetLoadAverage_unix()
{
  double loadavg[3] = { 0.0f, 0.0f, 0.0f };
  if (getloadavg(loadavg, 3) < 0)
  {
    // Maybe we should return an error here or the availability of
    // getloadavg(3) should be checked when ninja is configured.
    return -0.0f;
  }
  return loadavg[0];
}
#endif // _WIN32

double GetLoadAverage()
{
#ifdef _WIN32
  return GetLoadAverage_win32();
#else
  return GetLoadAverage_unix();
#endif // _WIN32
}
