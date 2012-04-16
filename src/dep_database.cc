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

// Dependency file database manager.
//
// Conceptually stores map<path, list<path>> representing dependents.
//
// Stored in a mapped file in two parts, the depindex and the deplist.
//
// depindex holds N fixed size records { char path[_MAX_PATH]; int offset; }
// offset points is a reference into the second part of the file. At each
// offset there is a chunk of memory loadable by Deplist::Load.
//
// Both ninja and ninja-deplist-helper map this file. ninja-deplist-helper is
// the writer, ninja is the reader. Access is protected via one global mutex
// for the whole file.
//
// Writing procedure:
//
// 1. Ensure paths are canonicalized, including lower case on Windows.
// 2. Build new serialized Deplist data to be added.
// 3. Acquire lock.
// 4a. If it's a new path to be added, append blob to deplist, add path to
// depindex and add path+offset to depindex.
// 4b. If it's an existing path, memcmp vs. old Deplist. If modified, append
// to deplist and point index at new entry.
// 5. Release lock.
//
// Reading procedure:
// 1. Acquire lock.
// 2. Ensure fully sorted.
// 3. Binary search for path and load associated Deplist.
// 4. Release lock.
//
// Defragment occasionally by locking, walking index and copying referenced
// data to a new file.


#include "dep_database.h"
#include "deplist.h"
#include "metrics.h"
#include "string_piece.h"
#include "util.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>

#pragma pack(push, 1)
struct DepIndex {
  char path[_MAX_PATH];
  int offset;
};

struct DbData {
  int index_entries;
  int max_index_entries;
  int dep_insert_offset;
  DepIndex index[1];
};
#pragma pack(pop)

enum { kInitialSize = 20000000 };

DepDatabase::DepDatabase(const string& filename, bool create) :
    view_(0),
    file_mapping_(0) {
  static const char* const mutex_name = "ninja_dep_database_mutex";
  if (create)
    lock_ = CreateMutex(NULL, TRUE, mutex_name);
  else
    lock_ = OpenMutex(MUTEX_ALL_ACCESS, FALSE, mutex_name);
  if (lock_ == NULL)
    Fatal("Couldn't Create/OpenMutex (%d)", GetLastError());

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
  bool initialize = false;
  if (size_ == 0) {
    initialize = true;
    IncreaseFileSize();
  }

  MapFile();

  if (initialize)
    SetEmptyData();

  // On CreateMutex, we acquire initial ownership so we can create the empty
  // file if necessary.
  if (create)
    Release();
}

DepDatabase::~DepDatabase() {
  UnmapFile();
  if (!CloseHandle(file_))
    Fatal("CloseHandle: file_");
  if (!CloseHandle(lock_))
    Fatal("CloseHandle: lock_");
}

bool PathCompare(const DepIndex& a, const DepIndex& b) {
  return strcmp(a.path, b.path) < 0;
}

const char* DepDatabase::FindDepData(const string& filename) {
  string file = filename;
  string err;

  // TODO: need to normcase too on windows
  CanonicalizePath(&file, &err); // TODO: assert this here, rather than doing it?

  const char* return_value = 0;
  Acquire();
  {
    DbData* view = GetView();
    DepIndex* end = &view->index[view->index_entries];
    DepIndex value;
    strcpy(value.path, file.c_str());
    DepIndex* i = lower_bound(view->index, end, value, PathCompare);
    if (i != end && strcmp(file.c_str(), i->path) == 0) {
      return_value = GetDataAt(i->offset);
    }
  }
  Release();
  return return_value;
}

void DepDatabase::InsertOrUpdateDepData(const string& filename,
                                        const char* data, int size) {
  string file = filename;
  string err;
  // TODO: need to normcase too on windows
  CanonicalizePath(&file, &err); // TODO: assert rather than doing here?

  // TODO: parse and assert dependent paths? Causes dependency on deplist, a
  // little annoying for tests.

  Acquire();
  {
    DbData* view = GetView();
    DepIndex* end = &view->index[view->index_entries];
    DepIndex value;
    strcpy(value.path, file.c_str());
    DepIndex* i = lower_bound(view->index, end, value, PathCompare);
    bool changed = false;
    if (i == end ||
        strcmp(file.c_str(), i->path) != 0 ||
        (changed = memcmp(GetDataAt(i->offset), data, size)) != 0) {
      // Don't already have it, or the deps have changed.

      while (view->dep_insert_offset + size > size_) {
        IncreaseFileSize();
      }
      // Append the new data.
      int inserted_offset = view->dep_insert_offset;
      char* insert_at = GetDataAt(view->dep_insert_offset);
      view->dep_insert_offset += size;
      memcpy(insert_at, data, size);

      if (changed) {
        // Updating, just point the old entry at the new data.
        i->offset = inserted_offset;
      } else {
        if (view->index_entries >= view->max_index_entries) {
          Fatal("need to grow index: %d entries", view->index_entries);
        }
        // We're inserting, not updating. Add to the index.
        DepIndex* elem = &view->index[view->index_entries++];
        strcpy(elem->path, file.c_str());
        elem->offset = inserted_offset;

        // TODO: defer sort until necessary (next lookup?)
        sort(view->index, &view->index[view->index_entries], PathCompare);
      }
    }
    // Otherwise, it's already there and hasn't changed.
  }
  Release();
}

void DepDatabase::Acquire() {
  if (WaitForSingleObject(lock_, INFINITE) != 0)
    Fatal("WaitForSingleObject (%d)", GetLastError());
}

void DepDatabase::Release() {
  ReleaseMutex(lock_);
}

DbData* DepDatabase::GetView() const {
  return reinterpret_cast<DbData*>(view_);
}

char* DepDatabase::GetDataAt(int offset) const {
  return reinterpret_cast<char*>(view_) + offset;
}

void DepDatabase::IncreaseFileSize() {
  UnmapFile();
  int target_size = size_ == 0 ? kInitialSize : size_ * 2;
  if (SetFilePointer(file_, target_size, NULL, FILE_BEGIN) ==
      INVALID_SET_FILE_POINTER)
    Fatal("SetFilePointer (%d)", GetLastError());
  if (!SetEndOfFile(file_))
    Fatal("SetEndOfFile (%d)", GetLastError());
  size_ = static_cast<int>(GetFileSize(file_, NULL));
  if (size_ != target_size)
    Fatal("deps database file resize failed");
  MapFile();
}

void DepDatabase::UnmapFile() {
  if (view_)
    if (!UnmapViewOfFile(view_))
      Fatal("UnmapViewOfFile");
  view_ = 0;
  if (file_mapping_)
    if (!CloseHandle(file_mapping_))
      Fatal("CloseHandle: file_mapping_");
  file_mapping_ = 0;
}

void DepDatabase::MapFile() {
  if (file_mapping_)
    return;
  file_mapping_ = CreateFileMapping(file_, NULL, PAGE_READWRITE, 0, 0, NULL);
  if (!file_mapping_)
    Fatal("Couldn't CreateFileMapping (%d)", GetLastError());
  view_ = MapViewOfFile(file_mapping_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
  if (!view_)
    Fatal("Couldn't MapViewOfFile (%d)", GetLastError());
}

void DepDatabase::SetEmptyData() {
  DbData* data = GetView();
  data->index_entries = 0;
  data->max_index_entries = 20000; // TODO random size
  data->dep_insert_offset = sizeof(DbData) + // TODO [1] too big
      sizeof(DepIndex) * data->max_index_entries;
  // TODO end of file/max size
}

void DepDatabase::DumpIndex() {
  Acquire();
  {
    DbData* view = GetView();
    for (int i = 0; i < view->index_entries; ++i) {
      printf("%d: %s @ %d\n", i, view->index[i].path, view->index[i].offset);
    }
  }
  Release();
}

void DepDatabase::DumpDeps(const string& filename) {
  Acquire();
  {
    const char* data = FindDepData(filename);
    if (data == 0) {
      printf("%s not found\n", filename.c_str());
      return;
    }
    vector<StringPiece> entries;
    string err;
    if (!Deplist::Load2(data, &entries, &err)) {
      printf("couldn't load deps for %s: %s\n", filename.c_str(), err.c_str());
      return;
    }
    printf("%s:\n", filename.c_str());
    for (vector<StringPiece>::const_iterator i = entries.begin();
         i != entries.end(); ++i) {
      string tmp(i->str_, i->len_);
      printf("  %s\n", tmp.c_str());
    }
  }
  Release();
}
