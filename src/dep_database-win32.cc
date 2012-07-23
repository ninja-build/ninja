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
#include "lockable_mapped_file.h"
#include "metrics.h"
#include "string_piece.h"
#include "util.h"

#include <algorithm>

#pragma warning(disable: 4200)
struct DepIndex {
  char path[_MAX_PATH];
  int offset;
};

struct DbData {
  int index_entries;
  int max_index_entries;
  int dep_insert_offset;
  DepIndex index[];
};

DepDatabase::DepDatabase(const string& filename, bool create,
                         int max_index_entries, int cleanup_size)
    : filename_(filename),
      data_(filename, create),
      max_index_entries_(max_index_entries) {

  if (!max_index_entries_)
    max_index_entries_ = 20000;
  if (!cleanup_size)
    cleanup_size = 500000000;

  if (data_.ShouldInitialize()) {
    SetEmptyData();
  } else {
    if (create && data_.Size() > cleanup_size) {
      CompactDatabase();
    }
  }
}

bool PathCompare(const DepIndex& a, const DepIndex& b) {
  return strcmp(a.path, b.path) < 0;
}

void DepDatabase::StartLookups() {
  data_.Acquire();
}

void DepDatabase::FinishLookups() {
  data_.Release();
}

bool DepDatabase::FindDepData(const string& filename,
                              vector<StringPiece>* deps,
                              string* err) {
  string file = filename;
  assert((CanonicalizePath(&file, err), file == filename));

  DbData* view = GetView();
  DepIndex* end = &view->index[view->index_entries];
  DepIndex value;
  strcpy(value.path, filename.c_str());
  DepIndex* i = lower_bound(view->index, end, value, PathCompare);
  if (i != end && strcmp(filename.c_str(), i->path) == 0) {
    const char* data = GetDataAt(i->offset);
    if (!Deplist::LoadNoHeader(data, deps, err))
      return false;
  }
  return true;
}

void DepDatabase::InsertOrUpdateDepData(const string& filename,
                                        const char* data, int size) {
  string file = filename;
  string err;
  // TODO: need to normcase too on windows
  CanonicalizePath(&file, &err); // TODO: assert rather than doing here?

  data_.Acquire();
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

      while (view->dep_insert_offset + size > data_.Size()) {
        data_.IncreaseFileSize();
        view = GetView();
        i = lower_bound(view->index, end, value, PathCompare);
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
  data_.Release();
}

// Once the file is grows past a certain size due to appending dep
// information, we compact it here.
void DepDatabase::CompactDatabase() {
  printf("ninja: Compacting dependency information...");
  fflush(stdout);
  string recompact_name = filename_ + ".recompact";
  {
    // In case it was left over, try to remove it. Failing is fine.
    DeleteFile(recompact_name.c_str());
    LockableMappedFile recompact(recompact_name, true);
    recompact.Acquire();
    data_.Acquire();
    DbData* source_view = GetView();
    DbData* compacted_view = reinterpret_cast<DbData*>(recompact.View());
    compacted_view->index_entries = source_view->index_entries;
    compacted_view->max_index_entries = source_view->max_index_entries;
    compacted_view->dep_insert_offset = sizeof(DbData) +
        sizeof(DepIndex) * compacted_view->max_index_entries;
    for (int i = 0; i < source_view->index_entries; ++i) {
      // Copy over file names, offsets are not updated yet.
      compacted_view->index[i] = source_view->index[i];
    }

    for (int i = 0; i < source_view->index_entries; ++i) {
      vector<StringPiece> deps;
      string err;
      size_t data_size;
      if (!FindDepData(source_view->index[i].path, &deps, &err))
        Fatal("couldn't get dep data for '%s'", source_view->index[i].path);
      char* deplist = Deplist::SerializeForDatabase(source_view->index[i].path,
                                                    deps, &data_size);

      // If we're out of space in the new one, grow it, and re-get the view.
      while (compacted_view->dep_insert_offset + int(data_size) >
                recompact.Size()) {
        recompact.IncreaseFileSize();
        compacted_view = reinterpret_cast<DbData*>(recompact.View());
      }
      char* insert_at = reinterpret_cast<char*>(recompact.View()) +
                        compacted_view->dep_insert_offset;
      memcpy(insert_at, deplist, data_size);
      compacted_view->index[i].offset = compacted_view->dep_insert_offset;
      compacted_view->dep_insert_offset += data_size;
      delete [] deplist;
    }
    data_.Release();
    recompact.Release();
  }
  data_.ReplaceDataFrom(recompact_name);
  printf("done.\n");
  fflush(stdout);
}

DbData* DepDatabase::GetView() const {
  return reinterpret_cast<DbData*>(data_.View());
}

char* DepDatabase::GetDataAt(int offset) const {
  return reinterpret_cast<char*>(data_.View()) + offset;
}

void DepDatabase::SetEmptyData() {
  data_.Acquire();
  DbData* data = GetView();
  data->index_entries = 0;
  data->max_index_entries = max_index_entries_;
  data->dep_insert_offset = sizeof(DbData) +
      sizeof(DepIndex) * data->max_index_entries;
  // TODO end of file/max size
  data_.Release();
}

void DepDatabase::DumpIndex(bool contents) {
  data_.Acquire();
  {
    DbData* view = GetView();
    for (int i = 0; i < view->index_entries; ++i) {
      printf("%d: %s", i, view->index[i].path);
      if (contents) {
        printf("\n");
        DumpDepsNoAcquire(view->index[i].path);
      } else {
        printf("@ %d\n", view->index[i].offset);
      }
    }
  }
  data_.Release();
}

void DepDatabase::DumpDepsNoAcquire(const string& filename) {
  vector<StringPiece> entries;
  string err;
  if (!FindDepData(filename, &entries, &err)) {
    printf("couldn't load deps for %s: %s\n", filename.c_str(), err.c_str());
  }
  printf("%s:\n", filename.c_str());
  for (vector<StringPiece>::const_iterator i = entries.begin();
       i != entries.end(); ++i) {
    string tmp(i->str_, i->len_);
    printf("  %s\n", tmp.c_str());
  }
}

void DepDatabase::DumpDeps(const string& filename) {
  data_.Acquire();
  DumpDepsNoAcquire(filename);
  data_.Release();
}

string DepDatabase::DumpToString() {
  string ret;
  data_.Acquire();
  {
    DbData* view = GetView();
    for (int i = 0; i < view->index_entries; ++i) {
      char buf[1024];
      sprintf(buf, "%d: %s\n", i, view->index[i].path);
      ret += string(buf);
      {
        vector<StringPiece> entries;
        string err;
        if (!FindDepData(view->index[i].path, &entries, &err))
          Fatal("couldn't load deps for %s: %s\n", view->index[i].path, err.c_str());
        ret += view->index[i].path;
        ret += ":\n";
        for (vector<StringPiece>::const_iterator i = entries.begin();
            i != entries.end(); ++i) {
          string tmp(i->str_, i->len_);
          ret += "  ";
          ret += tmp;
          ret += "\n";
        }
      }
    }
  }
  data_.Release();
  return ret;
}
