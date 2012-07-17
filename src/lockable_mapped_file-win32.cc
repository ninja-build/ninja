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

#include "lockable_mapped_file.h"

#include "util.h"

#include <assert.h>

static const char* kMutexSuffix = "_ninja_mutex";

string BuildMutexName(const string& filename) {
  char full_filename[_MAX_PATH];
  if (!GetFullPathName(filename.c_str(), sizeof(full_filename), full_filename, NULL))
    Fatal("GetFullPathName");
  for (char* p = full_filename; *p; ++p) {
    if (*p == '\\')
      *p = '_';
  }
  return string(full_filename) + kMutexSuffix;
}

LockableMappedFile::LockableMappedFile(const string& filename, bool create) :
    filename_(filename),
    view_(0),
    file_mapping_(0),
    should_initialize_(false),
    DEBUG_is_acquired_(create) {
  string mutex_name = BuildMutexName(filename);
  if (create)
    lock_ = CreateMutex(NULL, TRUE, mutex_name.c_str());
  else
    lock_ = OpenMutex(MUTEX_ALL_ACCESS, FALSE, mutex_name.c_str());
  if (lock_ == NULL)
    Fatal("Couldn't Create/OpenMutex (%d), create: %d", GetLastError(), create);

  if (create)
    file_ = CreateFile(filename.c_str(), GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
  if (!create || file_ == INVALID_HANDLE_VALUE)
    file_ = CreateFile(filename.c_str(), GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file_ == INVALID_HANDLE_VALUE)
    Fatal("Couldn't CreateFile (%d)", GetLastError());

  size_ = static_cast<int>(GetFileSize(file_, NULL));
  if (size_ == 0) {
    assert(create);
    should_initialize_ = true;
    IncreaseFileSize();
  }

  if (!create)
    Acquire();
  MapFile();
  Release();
}

LockableMappedFile::~LockableMappedFile() {
  Acquire();
  UnmapFile();
  if (!CloseHandle(file_))
    Fatal("CloseHandle: file_");
  Release();
  if (!CloseHandle(lock_))
    Fatal("CloseHandle: lock_");
}

void LockableMappedFile::Acquire() {
  assert(!DEBUG_is_acquired_);
  DWORD ret = WaitForSingleObject(lock_, INFINITE);
  DEBUG_is_acquired_ = true;
  if (ret != 0)
    Fatal("WaitForSingleObject (ret=%d, GLE=%d)", ret, GetLastError());
}

void LockableMappedFile::Release() {
  assert(DEBUG_is_acquired_);
  ReleaseMutex(lock_);
  DEBUG_is_acquired_ = false;
}

void LockableMappedFile::UnmapFile() {
  assert(DEBUG_is_acquired_);
  if (view_)
    if (!UnmapViewOfFile(view_))
      Fatal("UnmapViewOfFile");
  view_ = 0;
  if (file_mapping_)
    if (!CloseHandle(file_mapping_))
      Fatal("CloseHandle: file_mapping_");
  file_mapping_ = 0;
}

void LockableMappedFile::MapFile() {
  assert(DEBUG_is_acquired_);
  if (file_mapping_)
    return;
  file_mapping_ = CreateFileMapping(file_, NULL, PAGE_READWRITE, 0, 0, NULL);
  if (!file_mapping_)
    Fatal("Couldn't CreateFileMapping (%d)", GetLastError());
  view_ = MapViewOfFile(file_mapping_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
  if (!view_)
    Fatal("Couldn't MapViewOfFile (%d)", GetLastError());
}

enum { kInitialSize = 20000000 };

void LockableMappedFile::IncreaseFileSize() {
  assert(DEBUG_is_acquired_);
  UnmapFile();
  int target_size = size_ == 0 ? kInitialSize : size_ * 2;
  if (SetFilePointer(file_, target_size, NULL, FILE_BEGIN) ==
      INVALID_SET_FILE_POINTER)
    Fatal("SetFilePointer (%d)", GetLastError());
  if (!SetEndOfFile(file_))
    Fatal("SetEndOfFile (%d)", GetLastError());
  size_ = static_cast<int>(GetFileSize(file_, NULL));
  if (size_ != target_size)
    Fatal("file resize failed");
  MapFile();
}

void LockableMappedFile::ReplaceDataFrom(const string& filename) {
  Acquire();
  UnmapFile();
  CloseHandle(file_);
  if (!DeleteFile(filename_.c_str()))
    Fatal("DeleteFile (GLE=%d)", GetLastError());
  if (!MoveFile(filename.c_str(), filename_.c_str()))
    Fatal("MoveFile (GLE=%d)", GetLastError());
  file_ = CreateFile(filename_.c_str(), GENERIC_READ | GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  size_ = static_cast<int>(GetFileSize(file_, NULL));
  MapFile();
  Release();
}
