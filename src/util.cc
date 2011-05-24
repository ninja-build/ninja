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
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>

#include <vector>

#ifdef _WIN32
#include <direct.h>  // _mkdir
#endif

void Fatal(const char* msg, ...) {
  va_list ap;
  fprintf(stderr, "ninja: FATAL: ");
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fprintf(stderr, "\n");
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
  fprintf(stderr, "ninja: error: ");
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fprintf(stderr, "\n");
}

bool CanonicalizePath(string* path, string* err) {
  // WARNING: this function is performance-critical; please benchmark
  // any changes you make to it.

  // We don't want to allocate memory if necessary; a previous version
  // of this function modified |path| as it went, which meant we
  // needed to keep a copy of it around for including in a potential
  // error message.
  //
  // Instead, we find all path components within the path, then fix
  // them up to eliminate "foo/.." pairs and "." components.  Finally,
  // we overwrite path with these new substrings (since the path only
  // ever gets shorter, we can just use memmove within it).

  const int kMaxPathComponents = 30;
  const char* starts[kMaxPathComponents];  // Starts of path components.
  int lens[kMaxPathComponents];  // Lengths of path components.

  int parts_count = 0;  // Number of entries in starts/lens.
  int slash_count = 0;  // Number of components in the original path.
  for (string::size_type start = 0; start < path->size(); ++start) {
    string::size_type end = path->find('/', start);
    if (end == string::npos)
      end = path->size();
    if (end == 0 || end > start) {
      if (parts_count == kMaxPathComponents) {
        *err = "can't canonicalize path '" + *path + "'; too many "
            "path components";
        return false;
      }
      starts[parts_count] = path->data() + start;
      lens[parts_count] = end - start;
      ++parts_count;
    }
    ++slash_count;
    start = end;
  }

  int i = 0;
  while (i < parts_count) {
    const char* start = starts[i];
    int len = lens[i];
    if (start[0] == '.') {
      int strip_components = 0;
      if (len == 1) {
        // "."; strip this component.
        strip_components = 1;
      } else if (len == 2 && start[1] == '.') {
        // ".."; strip this and the previous component.
        if (i == 0) {
          *err = "can't canonicalize path '" + *path + "' that reaches "
            "above its directory";
          return false;
        }
        strip_components = 2;
        --i;
      }

      if (strip_components) {
        // Shift arrays backwards to remove bad path components.
        int entries_to_move = parts_count - i - strip_components;
        memmove(starts + i, starts + i + strip_components,
                sizeof(starts[0]) * entries_to_move);
        memmove(lens + i, lens + i + strip_components,
                sizeof(lens[0]) * entries_to_move);
        parts_count -= strip_components;
        continue;
      }
    }
    ++i;
  }

  if (parts_count == slash_count)
    return true;  // Nothing to do.

  char* p = (char*)path->data();
  for (i = 0; i < parts_count; ++i) {
    if (p > path->data())
      *p++ = '/';
    int len = lens[i];
    memmove(p, starts[i], len);
    p += len;
  }
  path->resize(p - path->data());

  return true;
}

int MakeDir(const string& path) {
#ifdef _WIN32
  return _mkdir(path.c_str());
#else
  return mkdir(path.c_str(), 0777);
#endif
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
