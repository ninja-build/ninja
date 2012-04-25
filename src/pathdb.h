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

// The ChangeJournal stores the raw FRN (a longlong) to the parent
// directory, but there's no fast, simple way to map that back to path,
// so we must maintain a database of paths here. This class represents
// map<DWORDLONG, pair<string, DWORDLONG>> where the RHS contains the
// name for a particular FRN and the FRN of its parent.

#ifndef NINJA_PATHDB_H
#define NINJA_PATHDB_H

#include <windows.h>
#include <vector>

#include "lockable_mapped_file.h"

struct PathDbEntry {
  DWORDLONG index;
  char name[_MAX_DIR];
  DWORDLONG parent_index;
};

#pragma warning(disable: 4200)
struct PathDbData {
  int num_entries;
  int max_entries;
  char drive_letter;
  DWORDLONG cur_journal_id;
  USN cur_usn;
  PathDbEntry entries[];
};

struct PathDb {
  PathDb(char drive_letter);
  void Add(DWORDLONG index, const string& name, DWORDLONG parent_index, bool defer_sort);
  string Get(DWORDLONG index, bool* err);
  bool Change(DWORDLONG index, const string& name, DWORDLONG parent_index);
  bool Delete(DWORDLONG index);
  void Populate(struct ChangeJournal& cj);
  vector<string> BulkGet(int num_entries, DWORDLONG* entries);

  char DriveLetter();
  DWORDLONG UsnJournalId();
  USN CurUsn();
private:
  friend struct ChangeJournal;
  void PrintStats();
  void SetEmptyData();
  void Sort();
  PathDbData* GetView();
  LockableMappedFile data_;
};

#endif  // NINJA_PATHDB_H
