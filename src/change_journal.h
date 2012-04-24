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

#ifndef NINJA_CHANGE_JOURNAL_H
#define NINJA_CHANGE_JOURNAL_H

#include <windows.h>

#include "pathdb.h"

struct InterestingPaths;
struct StatCache;

// Accessor of raw USN data.
struct ChangeJournal {
  ChangeJournal(char drive_letter,
                StatCache& stat_cache,
                InterestingPaths& interesting_paths);
  ~ChangeJournal();

  void CheckForDirtyPaths();
  bool ProcessAvailableRecords();
  void WaitForMoreData();
  const string& DriveLetter() const;
  char DriveLetterChar() const;

private:
  friend struct PathDb;
  void Query(USN_JOURNAL_DATA* usn);
  HANDLE SyncHandle() const { return cj_; }
  void PopulateStatFromDir(const string& path);

private:
  HANDLE Open(const string& drive_letter, bool async);
  void SeekToUsn(USN usn,
                 DWORD reason_mask,
                 bool return_only_on_close,
                 DWORDLONG usn_journal_id);
  bool SetUpNotification();
  PathDb pathdb_;
  StatCache& stat_cache_;
  InterestingPaths& interesting_paths_;
  USN_RECORD* Next(bool* err);
  string drive_letter_;

  // Handle to volume.
  HANDLE cj_;

  // Parameters for reading.
  READ_USN_JOURNAL_DATA rujd_;

  // Buffer of read data.
  BYTE cj_data_[32768];

  // Number of valid bytes in cj_data_.
  DWORD valid_cj_data_bytes_;

  // Pointer to current record.
  USN_RECORD* usn_record_;

  // Async reading used only for notification of new data.
  // Handle to volume, opened as async.
  HANDLE cj_async_;

  // Read buffer for async read.
  USN usn_async_;

  // Overlapped structure for async read.
  OVERLAPPED cj_async_overlapped_;
};

#endif  // NINJA_CHANGE_JOURNAL_H
