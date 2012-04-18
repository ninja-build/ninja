// Copyright 2012 Google Inc. All Rights Reserved.
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

#ifndef NINJA_LOCKABLE_MAPPED_FILE_H
#define NINJA_LOCKABLE_MAPPED_FILE_H

#include <string>
using namespace std;

#ifdef _WIN32
#include <windows.h>
#else
#error No non-Windows impl
#endif

struct LockableMappedFile {
  LockableMappedFile(const string& filename, bool create);
  ~LockableMappedFile();
  static bool IsAvailable(const string& filename);

  void Acquire();
  void Release();
  void IncreaseFileSize();
  int Size() { return size_; }
  void* View() const { return view_; }
  bool ShouldInitialize() { return should_initialize_; }

private:
  void UnmapFile();
  void MapFile();

  HANDLE lock_;
  HANDLE file_;
  HANDLE file_mapping_;
  void* view_;
  int size_;
  bool should_initialize_;
};

#endif  // NINJA_LOCKABLE_MAPPED_FILE_H
