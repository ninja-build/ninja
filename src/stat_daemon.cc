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


// Filter crap out by parent frn
//
// startup is whacked:
//   get current last usn
//   populate stat database
//   go into processing loop
//
// pathdb is only related to USN in that it needs to be flushed if usn
// read fails, otherwise populate, process are unrelated.
//
#include "includes_normalize.h"
#include "lockable_mapped_file.h"
#include "stat_cache.h"
#include "util.h"

#include <algorithm>
#include <assert.h>
#include <windows.h>
#include <winioctl.h>

namespace {

void Win32Fatal(const char* function) {
  Fatal("%s: %s", function, GetLastErrorString().c_str());
}

volatile bool gShutdown;
BOOL WINAPI NotifyInterrupted(DWORD dwCtrlType) {
  if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
    gShutdown = true;
    fclose(fopen("shutdown_notify", "w"));
    _unlink("shutdown_notify");
    return TRUE;
  }
  return FALSE;
}

string gBuildRoot;

void Log(const char* msg, ...) {
  va_list ap;
  fprintf(stdout, "ninja-stat-daemon: ");
  va_start(ap, msg);
  vfprintf(stdout, msg, ap);
  va_end(ap);
  fprintf(stdout, "\n");
}

// The ChangeJournal stores the raw FRN (a longlong) to the parent
// directory, but there's no fast, simple way to map that back to path,
// so we must maintain a database of paths here. This class represents
// map<DWORDLONG, pair<string, DWORDLONG>> where the RHS contains the
// name for a particular FRN and the FRN of its parent.

struct PathDbEntry {
  DWORDLONG index;
  char name[_MAX_DIR];
  DWORDLONG parent_index;
};

struct PathDbData {
  int num_entries;
  int max_entries;
  char drive_letter;
  DWORDLONG cur_journal_id;
  USN cur_usn;
  PathDbEntry entries[1];
};

struct PathDb {
  PathDb(char drive_letter);
  void Add(DWORDLONG index, const string& name, DWORDLONG parent_index, bool defer_sort);
  string Get(DWORDLONG index, bool* err);
  bool Change(DWORDLONG index, const string& name, DWORDLONG parent_index);
  bool Delete(DWORDLONG index);
  void Populate(struct ChangeJournal& cj);

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

// Accessor of raw USN data.
struct ChangeJournal {
  ChangeJournal(char drive_letter);
  ~ChangeJournal();
  void Query(USN_JOURNAL_DATA* usn);
  const string& DriveLetter() const;
  char DriveLetterChar() const;
  HANDLE SyncHandle() const { return cj_; }
  void CheckForDirtyPaths();
  void PopulateStatFromDir(const string& path);
  void ProcessAvailableRecords();
  void WaitForMoreData();

private:
  PathDb pathdb_;
  HANDLE Open(const string& drive_letter, bool async);
  void SeekToUsn(USN usn,
                 DWORD reason_mask,
                 bool return_only_on_close,
                 DWORDLONG usn_journal_id);
  bool SetUpNotification();
  StatCache stat_cache_;
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


PathDb::PathDb(char drive_letter) :
    data_((string(".ninja_stat_pathdb_") + drive_letter).c_str(), true) {
  if (data_.ShouldInitialize()) {
    SetEmptyData();
  }
  PrintStats();
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

  PrintStats();
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

ChangeJournal::ChangeJournal(char drive_letter) :
    pathdb_(drive_letter),
    stat_cache_(true) {
  assert(toupper(drive_letter) == drive_letter);
  drive_letter_ = string() + drive_letter;
  cj_ = Open(drive_letter_, false);
  if (cj_ == INVALID_HANDLE_VALUE)
    Win32Fatal("Open sync");
  cj_async_ = Open(drive_letter_, true);
  if (cj_async_ == INVALID_HANDLE_VALUE)
    Win32Fatal("Open async");
  memset(&cj_async_overlapped_, 0, sizeof(cj_async_overlapped_));
  cj_async_overlapped_.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (!cj_async_overlapped_.hEvent)
    Win32Fatal("CreateEvent");

  if (pathdb_.DriveLetter() != drive_letter)
    pathdb_.Populate(*this);
  SeekToUsn(pathdb_.CurUsn(), 0xffffffff, false, pathdb_.UsnJournalId());
}

ChangeJournal::~ChangeJournal() {
  CloseHandle(cj_);
  CloseHandle(cj_async_);
  SetEvent(cj_async_overlapped_.hEvent);
  CloseHandle(cj_async_overlapped_.hEvent);
}

HANDLE ChangeJournal::Open(const string& drive_letter, bool async) {
  string volume_path = string("\\\\.\\") + drive_letter + ":";
  HANDLE cj = CreateFile(
      volume_path.c_str(), GENERIC_WRITE | GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
      (async ? FILE_FLAG_OVERLAPPED : 0), NULL);
   return cj;
}

void ChangeJournal::Query(USN_JOURNAL_DATA* usn) {
  DWORD cb;
  bool success = DeviceIoControl(cj_, FSCTL_QUERY_USN_JOURNAL, NULL, 0,
                                 usn, sizeof(*usn), &cb, NULL);
  if (!success)
    Win32Fatal("DeviceIoControl, ChangeJournal::Query");
}

const string& ChangeJournal::DriveLetter() const {
  return drive_letter_;
}

char ChangeJournal::DriveLetterChar() const {
  return drive_letter_[0];
}

void ChangeJournal::SeekToUsn(USN usn,
                              DWORD reason_mask,
                              bool return_only_on_close,
                              DWORDLONG usn_journal_id) {
  rujd_.StartUsn = usn;
  rujd_.ReasonMask = reason_mask;
  rujd_.ReturnOnlyOnClose = return_only_on_close;
  rujd_.Timeout = 0;
  rujd_.BytesToWaitFor = 0;
  rujd_.UsnJournalID = usn_journal_id;
  valid_cj_data_bytes_ = 0;
  usn_record_ = 0;
}

string GetReasonString(DWORD reason) {
  static const char* reasons[] = {
    "DataOverwrite",         // 0x00000001
    "DataExtend",            // 0x00000002
    "DataTruncation",        // 0x00000004
    "0x00000008",            // 0x00000008
    "NamedDataOverwrite",    // 0x00000010
    "NamedDataExtend",       // 0x00000020
    "NamedDataTruncation",   // 0x00000040
    "0x00000080",            // 0x00000080
    "FileCreate",            // 0x00000100
    "FileDelete",            // 0x00000200
    "PropertyChange",        // 0x00000400
    "SecurityChange",        // 0x00000800
    "RenameOldName",         // 0x00001000
    "RenameNewName",         // 0x00002000
    "IndexableChange",       // 0x00004000
    "BasicInfoChange",       // 0x00008000
    "HardLinkChange",        // 0x00010000
    "CompressionChange",     // 0x00020000
    "EncryptionChange",      // 0x00040000
    "ObjectIdChange",        // 0x00080000
    "ReparsePointChange",    // 0x00100000
    "StreamChange",          // 0x00200000
    "0x00400000",            // 0x00400000
    "0x00800000",            // 0x00800000
    "0x01000000",            // 0x01000000
    "0x02000000",            // 0x02000000
    "0x04000000",            // 0x04000000
    "0x08000000",            // 0x08000000
    "0x10000000",            // 0x10000000
    "0x20000000",            // 0x20000000
    "0x40000000",            // 0x40000000
    "*Close*"                // 0x80000000
  };

  string ret;
  for (DWORD i = 0; reason != 0; reason >>= 1, ++i) {
    if (reason & 1) {
      if (ret != "")
        ret += ", ";
      ret += reasons[i];
    }
  }
  return ret;
}

bool ChangeJournal::SetUpNotification() {
  READ_USN_JOURNAL_DATA rujd;
  rujd = rujd_;
  rujd.BytesToWaitFor = 1;
  bool success = DeviceIoControl(cj_async_, FSCTL_READ_USN_JOURNAL,
      &rujd, sizeof(rujd), &usn_async_, sizeof(usn_async_), NULL,
      &cj_async_overlapped_);
  return !success && GetLastError() != ERROR_IO_PENDING;
}

void ChangeJournal::PopulateStatFromDir(const string& path) {
  Log("populate %s", path.c_str());
  string search_root = path + "\\";
  if (search_root == ".\\")
    search_root = "";
  string search = search_root + "*";
  WIN32_FIND_DATA find_data;
  HANDLE handle = FindFirstFile(search.c_str(), &find_data);
  if (handle == INVALID_HANDLE_VALUE)
    return;
  for (;;) {
    if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      string name = search_root +
                    IncludesNormalize::ToLower(find_data.cFileName);
      stat_cache_.NotifyChange(name);
    }
    BOOL success = FindNextFile(handle, &find_data);
    if (!success)
      break;
  }
  FindClose(handle);

}

void ChangeJournal::CheckForDirtyPaths() {
  int num_entries;
  DWORDLONG* entries;
  stat_cache_.StartProcessingChanges();
  if (stat_cache_.InterestingPathsDirtied(&num_entries, &entries)) {
    pathdb_.data_.Acquire();

    // TODO: Refresh!
    for (int i = 0; i < num_entries; ++i) {
      bool err = false;
      string dirname = pathdb_.Get(entries[i], &err);
      if (!err)
        PopulateStatFromDir(IncludesNormalize::Normalize(dirname, gBuildRoot.c_str()));
    }

    stat_cache_.ClearInterestingPathsDirtyFlag();
    pathdb_.data_.Release();
  }
  stat_cache_.FinishProcessingChanges();
}

void ChangeJournal::ProcessAvailableRecords() {
  for (;;) {
    stat_cache_.StartProcessingChanges();
    pathdb_.data_.Acquire();

    USN_RECORD* record;
    bool err = false;
    while ((record = Next(&err))) {
      wstring wide(reinterpret_cast<wchar_t*>(
                      reinterpret_cast<BYTE*>(record) + record->FileNameOffset),
                  record->FileNameLength / sizeof(WCHAR));
      string name(wide.begin(), wide.end());

      // If something's happening to a directory, we need to update the PathDb.
      if ((record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
          (record->Reason & USN_REASON_CLOSE)) {
        if (record->Reason & USN_REASON_FILE_CREATE)
          pathdb_.Add(record->FileReferenceNumber, name, record->ParentFileReferenceNumber, true);
        if (record->Reason & USN_REASON_RENAME_NEW_NAME)
          pathdb_.Change(record->FileReferenceNumber, name, record->ParentFileReferenceNumber);
        if (record->Reason & USN_REASON_FILE_DELETE)
          pathdb_.Delete(record->FileReferenceNumber);
      }

      // TODO We're almost definitely doing too much work here (e.g.
      // processing Open as well as Close, when only Close would be
      // sufficient). Stay conservative for now, which just means
      // extra _stats in our case.

      if (name.back() != '~' && // Ignore backup files, probably shouldn't.
          stat_cache_.IsInteresting(record->ParentFileReferenceNumber)) {
        bool err = false;
        string path = pathdb_.Get(record->ParentFileReferenceNumber, &err);
        if (!err) {
          string full_name = path + "\\" + name;
          string rel = IncludesNormalize::Normalize(full_name, gBuildRoot.c_str());
          //Log("%llx, %s: %s", record->Usn, rel.c_str(), GetReasonString(record->Reason).c_str());
          stat_cache_.NotifyChange(rel);
        }
      } else {
        //Log("ignoring %llx", record->Usn);
      }
      pathdb_.GetView()->cur_usn = record->Usn;
    }
    pathdb_.data_.Release();
    stat_cache_.FinishProcessingChanges();

    if (err)
      Win32Fatal("Next");

    // Normally, we'll break here. If we fail to set up async
    // notification though, just try to process again because more
    // data may have been received.
    if (SetUpNotification())
      break;
  }
}

USN_RECORD* ChangeJournal::Next(bool* err) {
  *err = false;
  if (usn_record_ == 0 ||
      reinterpret_cast<BYTE*>(usn_record_) + usn_record_->RecordLength >=
          cj_data_ + valid_cj_data_bytes_) {
    usn_record_ = NULL;
    BOOL success = DeviceIoControl(cj_, FSCTL_READ_USN_JOURNAL,
        &rujd_, sizeof(rujd_), cj_data_, sizeof(cj_data_), &valid_cj_data_bytes_, NULL);
    if (success) {
      rujd_.StartUsn = *reinterpret_cast<USN*>(cj_data_);
      if (valid_cj_data_bytes_ > sizeof(USN)) {
        usn_record_ = reinterpret_cast<USN_RECORD*>(&cj_data_[sizeof(USN)]);
      }
    } else {
      // Some check has failed. Records overflow, USN deleted, etc.
      // Cache needs to be fully flushed, as we can't trust any of it
      // now.
      *err = true;
    }
  } else {
    usn_record_ = reinterpret_cast<USN_RECORD*>(
        reinterpret_cast<BYTE*>(usn_record_) + usn_record_->RecordLength);
  }
  return usn_record_;
}

void ChangeJournal::WaitForMoreData() {
  //Log("sleeping");
  WaitForSingleObject(cj_async_overlapped_.hEvent, INFINITE);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    printf("usage: %s <build_root>\n", argv[0]);
    return 1;
  }
  // TODO: probably don't actually need this?
  char build_root[_MAX_PATH];
  if (!GetFullPathName(argv[1], sizeof(build_root), build_root, NULL)) {
    fprintf(stderr, "failed to get full path for build root\n");
    return 2;
  }
  gBuildRoot = build_root;

  if (!SetConsoleCtrlHandler(NotifyInterrupted, TRUE))
    Win32Fatal("SetConsoleCtrlHandler");
  gShutdown = false;
  Log("starting");
  ChangeJournal cj('C');
  while (!gShutdown) {
    cj.CheckForDirtyPaths();
    cj.ProcessAvailableRecords();
    cj.WaitForMoreData();
    // Wait a little to get some batch processing if there's a lot happening.
    Sleep(500);
  }
  Log("shutting down");
  return 0;
}
