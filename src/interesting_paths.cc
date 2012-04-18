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

#include "interesting_paths.h"

#include <algorithm>
#include <assert.h>
#include <windows.h>

// A set of all the parent FRNs that we wish to include in the stat
// database.
struct InterestingPathsData {
  int num_entries;
  int max_entries;
  bool dirty;
  DWORDLONG entries[1];
};

InterestingPaths::InterestingPaths(bool create) :
    data_(".ninja_interesting_paths", create) {
  if (data_.ShouldInitialize()) {
    data_.Acquire();
    InterestingPathsData* data = GetView();
    data->num_entries = 0;
    SetMaxEntries(data);
    data->dirty = false;
    data_.Release();
  }
}

void InterestingPaths::SetMaxEntries(InterestingPathsData* view) {
  view->max_entries = (data_.Size() - sizeof(InterestingPathsData)) / 
                       sizeof(DWORDLONG);
}

void InterestingPaths::StartAdditions() {
  data_.Acquire();
  num_entries_at_start_of_additions_ = GetView()->num_entries;
}

void InterestingPaths::FinishAdditions() {
  InterestingPathsData* data = GetView();
  sort(data->entries, &data->entries[data->num_entries]);
  DWORDLONG* new_end = unique(data->entries, &data->entries[data->num_entries] + 1);
  data->num_entries = new_end - data->entries;
  assert(data->num_entries >= num_entries_at_start_of_additions_);
  data->dirty = data->num_entries > num_entries_at_start_of_additions_;
  //printf("now %d entries\n", data->num_entries);
  data_.Release();
}

InterestingPathsData* InterestingPaths::GetView() {
  return reinterpret_cast<InterestingPathsData*>(data_.View());
}

// Must be bracketed in StartAdditions/FinishAdditions.
void InterestingPaths::Add(const string& path) {
  InterestingPathsData* data = GetView();
  if (data->num_entries >= data->max_entries) {
    data_.IncreaseFileSize();
    data = GetView();
    SetMaxEntries(data);
  }

  char full_path[_MAX_PATH];
  if (!GetFullPathName(path.c_str(), sizeof(full_path), full_path, NULL)) {
    printf("Couldn't get absolute path for %s\n", path.c_str());
    return;
  }
  //printf("Add %s\n", full_path);

  // Get FRN for directory containing given file.
  char dirname[_MAX_DIR];
  _splitpath(full_path, NULL, dirname, NULL, NULL);
  //printf("  dirname: %s\n", dirname);
  HANDLE dir_handle = CreateFile(dirname, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
      NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  BY_HANDLE_FILE_INFORMATION fi;
  GetFileInformationByHandle(dir_handle, &fi);
  CloseHandle(dir_handle);
  DWORDLONG parent_index = static_cast<DWORDLONG>(fi.nFileIndexHigh) << 32 |
                           static_cast<DWORDLONG>(fi.nFileIndexLow);

  //printf("  = %llx\n", parent_index);

  // Append to set. Is sorted/uniq'd in FinishAdditions.
  data->entries[data->num_entries++] = parent_index;
}

void InterestingPaths::StartLookups() {
  data_.Acquire();
}

bool InterestingPaths::IsPathInteresting(DWORDLONG index) {
  InterestingPathsData* data = GetView();
  DWORDLONG* end = &data->entries[data->num_entries];
  DWORDLONG* i = lower_bound(data->entries, end, index);
  return i != end && *i == index;
}

void InterestingPaths::FinishLookups() {
  data_.Release();
}
