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

#include "pathdb.h"

#include "change_journal.h"
#include "stat_daemon_util.h"

#include <algorithm>

PathDb::PathDb(char drive_letter) :
    data_((string(".ninja_stat_pathdb_") + drive_letter).c_str(), true) {
  if (data_.ShouldInitialize()) {
    SetEmptyData();
  }
  //PrintStats();
}

void PathDb::PrintStats() {
  data_.Acquire();
  PathDbData* data = GetView();
  Log("PathDb contains %d entries (%d max)", data->num_entries, data->max_entries);
  data_.Release();
}

void PathDb::SetEmptyData() {
  data_.Acquire();
  PathDbData* data = GetView();
  data->num_entries = 0;
  data->drive_letter = 0;
  data->cur_journal_id = 0;
  data->cur_usn = 0;
  data->max_entries = (data_.Size() - sizeof(PathDbData)) / sizeof(PathDbEntry);
  data_.Release();
}

PathDbData* PathDb::GetView() {
  return reinterpret_cast<PathDbData*>(data_.View());
}

void PathDb::Add(DWORDLONG index, const string& name, DWORDLONG parent_index, bool defer_sort) {
  PathDbData* data = GetView();
  if (data->num_entries >= data->max_entries) {
    data_.IncreaseFileSize();
    data = GetView();
    data->max_entries = (data_.Size() - sizeof(PathDbData)) / sizeof(PathDbEntry);
  }
  PathDbEntry* entry = &data->entries[data->num_entries++];
  entry->index = index;
  strcpy(entry->name, name.c_str());
  entry->parent_index = parent_index;
  if (!defer_sort)
    Sort();
}

bool FrnCompare(const PathDbEntry& a, const PathDbEntry& b) {
  return a.index < b.index;
}

string PathDb::Get(DWORDLONG index, bool* err) {
  string result;
  *err = false;
  do {
    // Lookup index to get name
    PathDbData* data = GetView();
    PathDbEntry* end = &data->entries[data->num_entries];
    PathDbEntry value;
    value.index = index;
    PathDbEntry* i = lower_bound(data->entries, end, value, FrnCompare);
    if (i == end || i->index != index) {
      *err = true;
    }
    result = string(i->name) + (result.size() > 0 ? "\\" : "") + result;
    index = i->parent_index;
  } while (index != 0);
  return result;
}

bool PathDb::Change(DWORDLONG index, const string& name, DWORDLONG parent_index) {
  PathDbData* data = GetView();
  PathDbEntry* end = &data->entries[data->num_entries];
  PathDbEntry value;
  value.index = index;
  PathDbEntry* i = lower_bound(data->entries, end, value, FrnCompare);
  if (i == end || i->index != index)
    return false;
  strcpy(i->name, name.c_str());
  i->parent_index = parent_index;
  return true;
}

bool PathDb::Delete(DWORDLONG index) {
  PathDbData* data = GetView();
  PathDbEntry* end = &data->entries[data->num_entries];
  PathDbEntry value;
  value.index = index;
  PathDbEntry* i = lower_bound(data->entries, end, value, FrnCompare);
  if (i == end || i->index != index)
    return false;
  *i = data->entries[--data->num_entries];
  Sort();
  return true;
}

void PathDb::Sort() {
  PathDbData* data = GetView();
  sort(data->entries, &data->entries[data->num_entries], FrnCompare);
}

void PathDb::Populate(ChangeJournal& cj) {
  Log("repopulating");
  SetEmptyData();
  USN_JOURNAL_DATA ujd;
  cj.Query(&ujd);

  data_.Acquire();

  // Get the FRN of the root.
  string root = cj.DriveLetter() + ":\\";
  HANDLE dir = CreateFile(root.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
      NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  BY_HANDLE_FILE_INFORMATION fi;
  GetFileInformationByHandle(dir, &fi);
  CloseHandle(dir);
  DWORDLONG root_index = static_cast<DWORDLONG>(fi.nFileIndexHigh) << 32 |
                         static_cast<DWORDLONG>(fi.nFileIndexLow);
  Add(root_index, cj.DriveLetter() + ":", 0, true);

  // Use the MFT to enumerate the rest of the disk.
  MFT_ENUM_DATA med;
  med.StartFileReferenceNumber = 0;
  med.LowUsn = 0;
  med.HighUsn = ujd.NextUsn;

  // Process in chunks.
  BYTE data[sizeof(DWORDLONG) + 0x10000];
  DWORD bytes_read;
  while (DeviceIoControl(cj.SyncHandle(), FSCTL_ENUM_USN_DATA, &med, sizeof(med),
        data, sizeof(data), &bytes_read, NULL) != false) {
    USN_RECORD* record = reinterpret_cast<USN_RECORD*>(&data[sizeof(USN)]);
    while (reinterpret_cast<BYTE*>(record) < data + bytes_read && !gShutdown) {
      if (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        wstring wide = wstring(
            reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(record) + record->FileNameOffset),
            record->FileNameLength / sizeof(WCHAR));
        string name(wide.begin(), wide.end());
        Add(record->FileReferenceNumber, name, record->ParentFileReferenceNumber, true);
      }
      record = reinterpret_cast<USN_RECORD*>(
          reinterpret_cast<BYTE*>(record) + record->RecordLength);
    }
    med.StartFileReferenceNumber = *reinterpret_cast<DWORDLONG*>(data);
  }

  // We deferred, sort now.
  Sort();

  GetView()->drive_letter = cj.DriveLetterChar();
  GetView()->cur_usn = ujd.NextUsn;
  GetView()->cur_journal_id = ujd.UsnJournalID;

  data_.Release();

  //PrintStats();
}

char PathDb::DriveLetter() {
  data_.Acquire();
  char ret = GetView()->drive_letter;
  data_.Release();
  return ret;
}

DWORDLONG PathDb::UsnJournalId() {
  data_.Acquire();
  DWORDLONG ret = GetView()->cur_journal_id;
  data_.Release();
  return ret;
}

USN PathDb::CurUsn() {
  data_.Acquire();
  USN ret = GetView()->cur_usn;
  data_.Release();
  return ret;
}
