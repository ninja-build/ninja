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

namespace {

std::string DirName(const std::string& path) {
#ifdef WIN32
  const char kPathSeparator = '\\';
#else
  const char kPathSeparator = '/';
#endif

  std::string::size_type slash_pos = path.rfind(kPathSeparator);
  if (slash_pos == std::string::npos)
    return std::string();  // Nothing to do.
  while (slash_pos > 0 && path[slash_pos - 1] == kPathSeparator)
    --slash_pos;
  return path.substr(0, slash_pos);
}

}  // namespace

bool DiskInterface::MakeDirs(const std::string& path) {
  std::string dir = DirName(path);
  if (dir.empty())
    return true;  // Reached root; assume it's there.
  int mtime = Stat(dir);
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
