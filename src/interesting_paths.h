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

#ifndef NINJA_INTERESTING_PATHS_H
#define NINJA_INTERESTING_PATHS_H

#include "lockable_mapped_file.h"

#include <string>
#include <vector>
using namespace std;

struct InterestingPaths {
  InterestingPaths(bool create);
  void StartAdditions();
  void Add(const string& path);
  void Add(const vector<string>& paths);
  void FinishAdditions();

  void StartLookups();
  bool IsInteresting(DWORDLONG index);
  bool IsDirty(int* num_entries, DWORDLONG** entries);
  void ClearDirty();
  void FinishLookups();
private:
  struct InterestingPathsData* GetView();
  void SetMaxEntries(struct InterestingPathsData* view);
  LockableMappedFile data_;
  int num_entries_at_start_of_additions_;
};

#endif  // NINJA_INTERESTING_PATHS_H
