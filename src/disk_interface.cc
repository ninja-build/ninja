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

// If _GNU_SOURCE is defined all the other features are turned on! ck
#ifndef _GNU_SOURCE
#define _GNU_SOURCE // needed for _LARGEFILE64_SOURCE, default on unix!
//XXX #define _POSIX_C_SOURCE
#endif

#include "disk_interface.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "util.h"

namespace {

string DirName(const string& path) {
#ifdef _WIN32
  const char kPathSeparator = '\\';
#else
  const char kPathSeparator = '/';
#endif

  string::size_type slash_pos = path.rfind(kPathSeparator);
  if (slash_pos == string::npos)
    return string();  // Nothing to do.
  while (slash_pos > 0 && path[slash_pos - 1] == kPathSeparator)
    --slash_pos;
  return path.substr(0, slash_pos);
}

}  // namespace

// DiskInterface ---------------------------------------------------------------

bool DiskInterface::MakeDirs(const string& path) {
  string dir = DirName(path);
  if (dir.empty())
    return true;  // Reached root; assume it's there.
  TimeStamp mtime = Stat(dir);
  if (mtime < 0)
    return false;  // Error.
  if (mtime > 0)
    return true;  // Exists already; we're done.

  // Directory doesn't exist.  Try creating its parent first.
  bool success = MakeDirs(dir);
  if (!success)
    return false;
  return MakeDir(dir);
}

// RealDiskInterface -----------------------------------------------------------

TimeStamp RealDiskInterface::Stat(const string& path) {
#ifdef _WIN32
  // MSDN: "Naming Files, Paths, and Namespaces"
  // http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247(v=vs.85).aspx
  if (!path.empty() && path[0] != '\\' && path.size() > MAX_PATH) {
    Error("Stat(%s): Filename longer than %i characters", path.c_str(), MAX_PATH);
    return -1;
  }
  WIN32_FILE_ATTRIBUTE_DATA attrs;
  if (!GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &attrs)) {
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
      return 0;
    Error("GetFileAttributesEx(%s): %s", path.c_str(),
          GetLastErrorString().c_str());
    return -1;
  }
  const FILETIME& filetime = attrs.ftLastWriteTime;
  /* A Windows file time is a 64-bit value that represents the number of
   * 100-nanosecond intervals that have elapsed since
   * 12:00 midnight, January 1, 1601 A.D. (C.E.)
   * Coordinated Universal Time (UTC). */
  uint64_t mtime = ((uint64_t)filetime.dwHighDateTime << 32) |
    ((uint64_t)filetime.dwLowDateTime);

#ifdef USE_TIME_T
  // We don't much care about epoch correctness but we do want the
  // resulting value to fit in an integer.
  mtime /= 1000000000LL / 100LL; // 100ns -> s.
  mtime -= 12622770400LL;  // 1600 epoch -> 2000 epoch (subtract 400 years).
  return (TimeStamp)mtime;
#else
  // return the time as a signed quadword, but keep 100nsec pression! ck
  return mtime;
#endif

#else

#ifdef USE_TIME_T
  struct stat st;
  if (stat(path.c_str(), &st) < 0) {
    if (errno == ENOENT || errno == ENOTDIR)
      return 0;
    Error("stat(%s): %s", path.c_str(), strerror(errno));
    return -1;
  }
  return st.st_mtime;
#else
#if defined(__CYGWIN__) || defined(_POSIX_C_SOURCE)
#define stat64 stat
#endif
  struct stat64 st;
  if (stat64(path.c_str(), &st) < 0) {
    if (errno == ENOENT || errno == ENOTDIR)
      return 0;
    Error("stat64(%s): %s", path.c_str(), strerror(errno));
    return -1;
  }
  // use 100 nsec ticks like windows
#if defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
  return (((int64_t) st.st_mtimespec.tv_sec) * 10000000LL) + (st.st_mtimespec.tv_nsec / 100LL);
#elif defined(_LARGEFILE64_SOURCE)
  return (((int64_t) st.st_mtim.tv_sec) * 10000000LL) + (st.st_mtim.tv_nsec / 100LL);
#else
  // see http://www.kernel.org/doc/man-pages/online/pages/man2/stat.2.html
  return (((int64_t) st.st_mtime) * 10000000LL) + (st.st_mtimensec / 100LL);
#endif
#endif  // USE_TIME_T

#endif
}

bool RealDiskInterface::WriteFile(const string& path, const string& contents) {
  FILE * fp = fopen(path.c_str(), "w");
  if (fp == NULL) {
    Error("WriteFile(%s): Unable to create file. %s", path.c_str(), strerror(errno));
    return false;
  }

  if (fwrite(contents.data(), 1, contents.length(), fp) < contents.length())  {
    Error("WriteFile(%s): Unable to write to the file. %s", path.c_str(), strerror(errno));
    fclose(fp);
    return false;
  }

  if (fclose(fp) == EOF) {
    Error("WriteFile(%s): Unable to close the file. %s", path.c_str(), strerror(errno));
    return false;
  }

  return true;
}

bool RealDiskInterface::MakeDir(const string& path) {
  if (::MakeDir(path) < 0) {
    Error("mkdir(%s): %s", path.c_str(), strerror(errno));
    return false;
  }
  return true;
}

string RealDiskInterface::ReadFile(const string& path, string* err) {
  string contents;
  int ret = ::ReadFile(path, &contents, err);
  if (ret == -ENOENT) {
    // Swallow ENOENT.
    err->clear();
  }
  return contents;
}

int RealDiskInterface::RemoveFile(const string& path) {
  if (remove(path.c_str()) < 0) {
    switch (errno) {
      case ENOENT:
        return 1;
      default:
        Error("remove(%s): %s", path.c_str(), strerror(errno));
        return -1;
    }
  } else {
    return 0;
  }
}
