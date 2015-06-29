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

#include <algorithm>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <sstream>
#include <windows.h>
#include <direct.h>  // _mkdir
#endif

#include "util.h"

namespace {

string DirName(const string& path) {
#ifdef _WIN32
  const char kPathSeparators[] = "\\/";
#else
  const char kPathSeparators[] = "/";
#endif
  string::size_type slash_pos = path.find_last_of(kPathSeparators);
  if (slash_pos == string::npos)
    return string();  // Nothing to do.
  const char* const kEnd = kPathSeparators + strlen(kPathSeparators);
  while (slash_pos > 0 &&
         std::find(kPathSeparators, kEnd, path[slash_pos - 1]) != kEnd)
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

#ifdef _WIN32
TimeStamp TimeStampFromFileTime(const FILETIME& filetime) {
  // FILETIME is in 100-nanosecond increments since the Windows epoch.
  // We don't much care about epoch correctness but we do want the
  // resulting value to fit in an integer.
  uint64_t mtime = ((uint64_t)filetime.dwHighDateTime << 32) |
    ((uint64_t)filetime.dwLowDateTime);
  mtime /= 1000000000LL / 100; // 100ns -> s.
  mtime -= 12622770400LL;  // 1600 epoch -> 2000 epoch (subtract 400 years).
  return (TimeStamp)mtime;
}

TimeStamp StatSingleFile(const string& path, string* err) {
  WIN32_FILE_ATTRIBUTE_DATA attrs;
  if (!GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &attrs)) {
    DWORD win_err = GetLastError();
    if (win_err == ERROR_FILE_NOT_FOUND || win_err == ERROR_PATH_NOT_FOUND)
      return 0;
    *err = "GetFileAttributesEx(" + path + "): " + GetLastErrorString();
    return -1;
  }
  return TimeStampFromFileTime(attrs.ftLastWriteTime);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)  // GetVersionExA is deprecated post SDK 8.1.
#endif
bool IsWindows7OrLater() {
  OSVERSIONINFO version_info = { sizeof(version_info) };
  if (!GetVersionEx(&version_info))
    Fatal("GetVersionEx: %s", GetLastErrorString().c_str());
  return version_info.dwMajorVersion > 6 ||
         (version_info.dwMajorVersion == 6 && version_info.dwMinorVersion >= 1);
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

bool StatAllFilesInDir(const string& dir, map<string, TimeStamp>* stamps,
                       string* err) {
  // FindExInfoBasic is 30% faster than FindExInfoStandard.
  static bool can_use_basic_info = IsWindows7OrLater();
  // This is not in earlier SDKs.
  const FINDEX_INFO_LEVELS kFindExInfoBasic =
      static_cast<FINDEX_INFO_LEVELS>(1);
  FINDEX_INFO_LEVELS level =
      can_use_basic_info ? kFindExInfoBasic : FindExInfoStandard;
  WIN32_FIND_DATAA ffd;
  HANDLE find_handle = FindFirstFileExA((dir + "\\*").c_str(), level, &ffd,
                                        FindExSearchNameMatch, NULL, 0);

  if (find_handle == INVALID_HANDLE_VALUE) {
    DWORD win_err = GetLastError();
    if (win_err == ERROR_FILE_NOT_FOUND || win_err == ERROR_PATH_NOT_FOUND)
      return true;
    *err = "FindFirstFileExA(" + dir + "): " + GetLastErrorString();
    return false;
  }
  do {
    string lowername = ffd.cFileName;
    transform(lowername.begin(), lowername.end(), lowername.begin(), ::tolower);
    stamps->insert(make_pair(lowername,
                             TimeStampFromFileTime(ffd.ftLastWriteTime)));
  } while (FindNextFileA(find_handle, &ffd));
  FindClose(find_handle);
  return true;
}
#endif  // _WIN32

}  // namespace

// DiskInterface ---------------------------------------------------------------

bool DiskInterface::MakeDirs(const string& path) {
  string dir = DirName(path);
  if (dir.empty())
    return true;  // Reached root; assume it's there.
  string err;
  TimeStamp mtime = Stat(dir, &err);
  if (mtime < 0) {
    Error("%s", err.c_str());
    return false;
  }
  if (mtime > 0)
    return true;  // Exists already; we're done.

  // Directory doesn't exist.  Try creating its parent first.
  bool success = MakeDirs(dir);
  if (!success)
    return false;
  return MakeDir(dir);
}

// RealDiskInterface -----------------------------------------------------------

TimeStamp RealDiskInterface::Stat(const string& path, string* err) const {
#ifdef _WIN32
  // MSDN: "Naming Files, Paths, and Namespaces"
  // http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247(v=vs.85).aspx
  if (!path.empty() && path[0] != '\\' && path.size() > MAX_PATH) {
    ostringstream err_stream;
    err_stream << "Stat(" << path << "): Filename longer than " << MAX_PATH
               << " characters";
    *err = err_stream.str();
    return -1;
  }
  if (!use_cache_)
    return StatSingleFile(path, err);

  string dir = DirName(path);
  string base(path.substr(dir.size() ? dir.size() + 1 : 0));

  transform(dir.begin(), dir.end(), dir.begin(), ::tolower);
  transform(base.begin(), base.end(), base.begin(), ::tolower);

  Cache::iterator ci = cache_.find(dir);
  if (ci == cache_.end()) {
    ci = cache_.insert(make_pair(dir, DirCache())).first;
    if (!StatAllFilesInDir(dir.empty() ? "." : dir, &ci->second, err)) {
      cache_.erase(ci);
      return -1;
    }
  }
  DirCache::iterator di = ci->second.find(base);
  return di != ci->second.end() ? di->second : 0;
#else
  struct stat st;
  if (stat(path.c_str(), &st) < 0) {
    if (errno == ENOENT || errno == ENOTDIR)
      return 0;
    *err = "stat(" + path + "): " + strerror(errno);
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
    if (errno == EEXIST) {
      return true;
    }
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

void RealDiskInterface::AllowStatCache(bool allow) {
#ifdef _WIN32
  use_cache_ = allow;
  if (!use_cache_)
    cache_.clear();
#endif
}
