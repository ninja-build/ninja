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

#include "change_journal.h"

#include "includes_normalize.h"
#include "stat_daemon_util.h"

#include <algorithm>
#include <assert.h>
#include <windows.h>
#include <winioctl.h>

ChangeJournal::ChangeJournal(char drive_letter, StatCache& stat_cache) :
    pathdb_(drive_letter),
    stat_cache_(stat_cache) {
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
      stat_cache_.NotifyChange(name,
                               FiletimeToTimestamp(find_data.ftLastWriteTime),
                               true);
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

    stat_cache_.EmptyCache();

    for (int i = 0; i < num_entries; ++i) {
      bool err = false;
      string dirname = pathdb_.Get(entries[i], &err);
      printf("ENTRY: %d %s\n", i, dirname.c_str());
      if (!err)
        PopulateStatFromDir(IncludesNormalize::Normalize(dirname, gBuildRoot.c_str()));
    }

    stat_cache_.Sort();

    stat_cache_.ClearInterestingPathsDirtyFlag();
    pathdb_.data_.Release();
  }
  stat_cache_.FinishProcessingChanges();
}

bool ChangeJournal::ProcessAvailableRecords() {
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

      if (record->Reason & USN_REASON_HARD_LINK_CHANGE) {
        // The name we receive in this notification is the target, but there's
        // no information about any of the links. So, use
        // FindFirst/NextFileNameW to walk all the hard links to this file,
        // and notify about all of them.
        string path = pathdb_.Get(record->ParentFileReferenceNumber, &err);
        if (!err) {
          string full_name = path + "\\" + name;
          wchar_t name[_MAX_PATH];
          DWORD len = sizeof(name);
          wstring wide_name(full_name.begin(), full_name.end());
          HANDLE handle = FindFirstFileNameW(wide_name.c_str(), 0, &len, name);
          if (handle == INVALID_HANDLE_VALUE)
            return false;
          for (;;) {
            string narrow(name, &name[len]);
            string rel = IncludesNormalize::Normalize(narrow, gBuildRoot.c_str());
            stat_cache_.NotifyChange(rel, -1, false);
            Log("hardlink: %s", rel.c_str());
            BOOL success = FindNextFileNameW(handle, &len, name);
            if (!success)
              break;
          }
          FindClose(handle);
        }
      }

      // TODO We're almost definitely doing too much work here (e.g.
      // processing Open as well as Close, when only Close would be
      // sufficient). Stay conservative for now, which just means
      // extra _stats in our case.

      bool ignore = name.back() == '~' || // Ignore backup files, probably shouldn't.
          !stat_cache_.IsInteresting(record->ParentFileReferenceNumber);
      if (!ignore) {
        bool err = false;
        string path = pathdb_.Get(record->ParentFileReferenceNumber, &err);
        if (!err) {
          string full_name = path + "\\" + name;
          string rel = IncludesNormalize::Normalize(full_name, gBuildRoot.c_str());
          stat_cache_.NotifyChange(rel, -1, false);
          //Log("%llx, %s: %s", record->Usn, rel.c_str(),
              //GetReasonString(record->Reason).c_str());
        } else {
          // Can happen if the parent directory is removed before we
          // process this record, if we don't have access to it, etc.
          //Log("error for %llx\n", record->ParentFileReferenceNumber);
        }
      } else {
        //Log("%llx ignored");
      }
      pathdb_.GetView()->cur_usn = record->Usn;
    }
    pathdb_.data_.Release();
    stat_cache_.FinishProcessingChanges();

    if (err) {
      // Something bad happened: maybe the journal overflowed, didn't exist,
      // etc. Try starting over.
      return false;
    }

    // Normally, we'll break here. If we fail to set up async
    // notification though, just try to process again because more
    // data may have been received.
    if (SetUpNotification())
      break;
  }
  return true;
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
