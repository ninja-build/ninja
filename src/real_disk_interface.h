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

#ifndef NINJA_REAL_DISK_INTERFACE_H_
#define NINJA_REAL_DISK_INTERFACE_H_

#include <string>

#include "disk_interface.h"

/// Implementation of DiskInterface that actually hits the disk.
struct RealDiskInterface : public DiskInterface {
  virtual ~RealDiskInterface() {}
  virtual int Stat(const std::string& path);
  virtual bool MakeDir(const std::string& path);
  virtual std::string ReadFile(const std::string& path, std::string* err);
  virtual int RemoveFile(const std::string& path);
};

#endif  // NINJA_REAL_DISK_INTERFACE_H_
