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

#ifndef NINJA_DISK_INTERFACE_H_
#define NINJA_DISK_INTERFACE_H_

#include <map>
#include <string>
using namespace std;

#include "timestamp.h"

/// Interface for accessing the disk.
///
/// Abstract so it can be mocked out for tests.  The real implementation
/// is RealDiskInterface.
struct DiskInterface {
  virtual ~DiskInterface() {}

  /// stat() a file, returning the mtime, or 0 if missing and -1 on
  /// other errors.
  virtual TimeStamp Stat(const string& path, string* err) const = 0;

  /// Create a directory, returning false on failure.
  virtual bool MakeDir(const string& path) = 0;

  /// Create a file, with the specified name and contents
  /// Returns true on success, false on failure
  virtual bool WriteFile(const string& path, const string& contents) = 0;

  /// Read a file to a string.  Fill in |err| on error.
  virtual string ReadFile(const string& path, string* err) = 0;

  /// Remove the file named @a path. It behaves like 'rm -f path' so no errors
  /// are reported if it does not exists.
  /// @returns 0 if the file has been removed,
  ///          1 if the file does not exist, and
  ///          -1 if an error occurs.
  virtual int RemoveFile(const string& path) = 0;

  /// Create all the parent directories for path; like mkdir -p
  /// `basename path`.
  bool MakeDirs(const string& path);
};

/// Implementation of DiskInterface that actually hits the disk.
struct RealDiskInterface : public DiskInterface {
  RealDiskInterface()
#ifdef _WIN32
                      : use_cache_(false)
#endif
                      {}
  virtual ~RealDiskInterface() {}
  virtual TimeStamp Stat(const string& path, string* err) const;
  virtual bool MakeDir(const string& path);
  virtual bool WriteFile(const string& path, const string& contents);
  virtual string ReadFile(const string& path, string* err);
  virtual int RemoveFile(const string& path);

  /// Whether stat information can be cached.  Only has an effect on Windows.
  void AllowStatCache(bool allow);

 private:
#ifdef _WIN32
  /// Whether stat information can be cached.
  bool use_cache_;

  typedef map<string, TimeStamp> DirCache;
  // TODO: Neither a map nor a hashmap seems ideal here.  If the statcache
  // works out, come up with a better data structure.
  typedef map<string, DirCache> Cache;
  mutable Cache cache_;
#endif
};

#endif  // NINJA_DISK_INTERFACE_H_
