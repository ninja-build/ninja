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
#include <share.h>
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
#include <unistd.h>
#include <sys/time.h>
#endif

#include <vector>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/sysctl.h>
#elif defined(__SVR4) && defined(__sun)
#include <unistd.h>
#include <sys/loadavg.h>
#elif defined(linux) || defined(__GLIBC__)
#include <sys/sysinfo.h>
#endif

#include "edit_distance.h"
#include "metrics.h"

void Fatal(const char* msg, ...) {
  va_list ap;
  fprintf(stderr, "ninja: fatal: ");
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fprintf(stderr, "\n");
#ifdef _WIN32
  // On Windows, some tools may inject extra threads.
  // exit() may block on locks held by those threads, so forcibly exit.
  fflush(stderr);
  fflush(stdout);
  ExitProcess(1);
#else
  exit(1);
#endif
}

void Warning(const char* msg, ...) {
  va_list ap;
  fprintf(stderr, "ninja: warning: ");
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fprintf(stderr, "\n");
}

void Error(const char* msg, ...) {
  va_list ap;
  fprintf(stderr, "ninja: error: ");
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fprintf(stderr, "\n");
}

bool CanonicalizePath(string* path, string* err) {
  METRIC_RECORD("canonicalize str");
  size_t len = path->size();
  char* str = 0;
  if (len > 0)
    str = &(*path)[0];
  if (!CanonicalizePath(str, &len, err))
    return false;
  path->resize(len);
  return true;
}

bool CanonicalizePath(char* path, size_t* len, string* err) {
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

  if (*src == '/') {
#ifdef _WIN32
    // network path starts with //
    if (*len > 1 && *(src + 1) == '/') {
      src += 2;
      dst += 2;
    } else {
      ++src;
      ++dst;
    }
#else
    ++src;
    ++dst;
#endif
  }

  while (src < end) {
    if (*src == '.') {
      if (src + 1 == end || src[1] == '/') {
        // '.' component; eliminate.
        src += 2;
        continue;
      } else if (src[1] == '.' && (src + 2 == end || src[2] == '/')) {
        // '..' component.  Back up if possible.
        if (component_count > 0) {
          dst = components[component_count - 1];
          src += 3;
          --component_count;
        } else {
          *dst++ = *src++;
          *dst++ = *src++;
          *dst++ = *src++;
        }
        continue;
      }
    }

    if (*src == '/') {
      src++;
      continue;
    }

    if (component_count == kMaxPathComponents)
      Fatal("path has too many components : %s", path);
    components[component_count] = dst;
    ++component_count;

    while (*src != '/' && src != end)
      *dst++ = *src++;
    *dst++ = *src++;  // Copy '/' or final \0 character as well.
  }

  if (dst == start) {
    *err = "path canonicalizes to the empty path";
    return false;
  }

  *len = dst - start - 1;
  return true;
}

int ReadFile(const string& path, string* contents, string* err) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) {
    err->assign(strerror(errno));
    return -errno;
  }

  char buf[64 << 10];
  size_t len;
  while ((len = fread(buf, 1, sizeof(buf), f)) > 0) {
    contents->append(buf, len);
  }
  if (ferror(f)) {
    err->assign(strerror(errno));  // XXX errno?
    contents->clear();
    fclose(f);
    return -errno;
  }
  fclose(f);
  return 0;
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

const char* SpellcheckString(const char* text, ...) {
  // Note: This takes a const char* instead of a string& because using
  // va_start() with a reference parameter is undefined behavior.
  va_list ap;
  va_start(ap, text);
  vector<const char*> words;
  const char* word;
  while ((word = va_arg(ap, const char*)))
    words.push_back(word);
  va_end(ap);
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

void Win32Fatal(const char* function) {
  Fatal("%s: %s", function, GetLastErrorString().c_str());
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

int GetProcessorCount() {
#ifdef _WIN32
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwNumberOfProcessors;
#else
  return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

#if defined(_WIN32) || defined(__CYGWIN__)
double GetLoadAverage() {
  // TODO(nicolas.despres@gmail.com): Find a way to implement it on Windows.
  // Remember to also update Usage() when this is fixed.
  return -0.0f;
}
#else
double GetLoadAverage() {
  double loadavg[3] = { 0.0f, 0.0f, 0.0f };
  if (getloadavg(loadavg, 3) < 0) {
    // Maybe we should return an error here or the availability of
    // getloadavg(3) should be checked when ninja is configured.
    return -0.0f;
  }
  return loadavg[0];
}
#endif // _WIN32

string ElideMiddle(const string& str, size_t width) {
  const int kMargin = 3;  // Space for "...".
  string result = str;
  if (result.size() + kMargin > width) {
    size_t elide_size = (width - kMargin) / 2;
    result = result.substr(0, elide_size)
      + "..."
      + result.substr(result.size() - elide_size, elide_size);
  }
  return result;
}

bool Truncate(const string& path, size_t size, string* err) {
#ifdef _WIN32
  int fh = _sopen(path.c_str(), _O_RDWR | _O_CREAT, _SH_DENYNO,
                  _S_IREAD | _S_IWRITE);
  int success = _chsize(fh, size);
  _close(fh);
#else
  int success = truncate(path.c_str(), size);
#endif
  // Both truncate() and _chsize() return 0 on success and set errno and return
  // -1 on failure.
  if (success < 0) {
    *err = strerror(errno);
    return false;
  }
  return true;
}
