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

#include "disk_interface.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>  // _mkdir
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

int MakeDir(const string& path) {
#ifdef _WIN32
  return _mkdir(path.c_str());
#else
  return mkdir(path.c_str(), 0777);
#endif
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
    if (!quiet_) {
      Error("Stat(%s): Filename longer than %i characters",
            path.c_str(), MAX_PATH);
    }
    return -1;
  }
  WIN32_FILE_ATTRIBUTE_DATA attrs;
  if (!GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &attrs)) {
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
      return 0;
    if (!quiet_) {
      Error("GetFileAttributesEx(%s): %s", path.c_str(),
            GetLastErrorString().c_str());
    }
    return -1;
  }
  const FILETIME& filetime = attrs.ftLastWriteTime;
  // FILETIME is in 100-nanosecond increments since the Windows epoch.
  // We don't much care about epoch correctness but we do want the
  // resulting value to fit in an integer.
  uint64_t mtime = ((uint64_t)filetime.dwHighDateTime << 32) |
    ((uint64_t)filetime.dwLowDateTime);
  mtime /= 1000000000LL / 100; // 100ns -> s.
  mtime -= 12622770400LL;  // 1600 epoch -> 2000 epoch (subtract 400 years).
  return (TimeStamp)mtime;
#else
  struct stat st;
  if (stat(path.c_str(), &st) < 0) {
    if (errno == ENOENT || errno == ENOTDIR)
      return 0;
    if (!quiet_) {
      Error("stat(%s): %s", path.c_str(), strerror(errno));
    }
    return -1;
  }
  return st.st_mtime;
#endif
}

bool RealDiskInterface::WriteFile(const string& path, const string& contents) {
  FILE* fp = fopen(path.c_str(), "w");
  if (fp == NULL) {
    Error("WriteFile(%s): Unable to create file. %s",
          path.c_str(), strerror(errno));
    return false;
  }

  if (fwrite(contents.data(), 1, contents.length(), fp) < contents.length())  {
    Error("WriteFile(%s): Unable to write to the file. %s",
          path.c_str(), strerror(errno));
    fclose(fp);
    return false;
  }

  if (fclose(fp) == EOF) {
    Error("WriteFile(%s): Unable to close the file. %s",
          path.c_str(), strerror(errno));
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
