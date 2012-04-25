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

// Windows-only (currently) timestamp cache.

#include "stat_cache.h"

#include "disk_interface.h"
#include "metrics.h"

#include <algorithm>

namespace {

TimeStamp StatPath(const string& path) {
  WIN32_FILE_ATTRIBUTE_DATA attrs;
  if (!GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &attrs)) {
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
      return 0;
    Error("GetFileAttributesEx(%s): %s", path.c_str(),
          GetLastErrorString().c_str());
    return -1;
  }
  return FiletimeToTimestamp(attrs.ftLastWriteTime);
}

}  // namespace


// |path| stored as normalized (relative to build root, normalized slashes,
// lower case, etc.).
// TODO: indirect and make paths pointers? would probably save a decent chunk
// of space which might make lookups faster (though would have to jump more).
struct StatCacheEntry {
  char path[_MAX_PATH];
  TimeStamp mtime;
};

bool StatCachePathCompare(const StatCacheEntry& a, const StatCacheEntry& b) {
  return strcmp(a.path, b.path) < 0;
}

#pragma warning(disable: 4200)
struct StatCacheData {
  int num_entries;
  int max_entries;
  StatCacheEntry entries[];
};

static const char* kStatCacheFileName = ".ninja_stat_cache";

// static
int StatCache::is_active_ = -1;

// static
bool StatCache::Active() {
  if (is_active_ < 0) {
    char* env = getenv("NINJA_STAT_DAEMON");
    if (env)
      is_active_ = atoi(env);
  }
  return is_active_ > 0;
}

// static
void StatCache::EnsureDaemonRunning() {
  if (LockableMappedFile::IsAvailable(kStatCacheFileName))
    return;
  printf("ninja: starting stat daemon\n");
  STARTUPINFOA startup_info;
  memset(&startup_info, 0, sizeof(startup_info));
  startup_info.cb = sizeof(STARTUPINFO);
  startup_info.dwFlags = STARTF_USESTDHANDLES;
  startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup_info.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  PROCESS_INFORMATION process_info;
  memset(&process_info, 0, sizeof(process_info));
  if (!CreateProcessA(NULL, "ninja-stat-daemon .", NULL, NULL,
                      /* inherit handles */ TRUE, CREATE_NEW_PROCESS_GROUP,
                      NULL, NULL,
                      &startup_info, &process_info)) {
    Fatal("Couldn't launch stat-daemon: GLE: %d", GetLastError());
  }
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);

  int count = 0;
  while (!LockableMappedFile::IsAvailable(kStatCacheFileName)) {
    Sleep(200);
    ++count;
    /*
    if (count == 20)
      Fatal("Couldn't start daemon?");
      */
  }
}

// static
void StatCache::Dump() {
  StatCache stat_cache(false, NULL);
  stat_cache.StartBuild();
  StatCacheData* data = stat_cache.GetView();
  for (int i = 0; i < data->num_entries; ++i) {
    printf("%d: %s -> %d\n", i, data->entries[i].path, data->entries[i].mtime);
  }
  stat_cache.FinishBuild();
}

// static
void StatCache::ValidateAgainstDisk(DiskInterface& disk_interface) {
  StatCache stat_cache(false, NULL);
  stat_cache.StartBuild();
  StatCacheData* data = stat_cache.GetView();
  for (int i = 0; i < data->num_entries; ++i) {
    int on_disk = disk_interface.Stat(data->entries[i].path);
    if (data->entries[i].mtime != on_disk) {
      printf("%s differs: %d vs %d\n",
          data->entries[i].path, data->entries[i].mtime, on_disk);
    }
  }
  stat_cache.FinishBuild();
}

StatCache::StatCache(bool create, DiskInterface* disk_interface)
    : data_(kStatCacheFileName, create),
      disk_interface_(disk_interface) {
  if (data_.ShouldInitialize()) {
    data_.Acquire();
    StatCacheData* data = GetView();
    data->num_entries = 0;
    data->max_entries = (data_.Size() - sizeof(StatCacheData)) /
                        sizeof(StatCacheEntry);
    data_.Release();
  }
}

void StatCache::StartBuild() {
  data_.Acquire();
}

static bool PathCompare(const StatCacheEntry& a, const StatCacheEntry& b) {
  return strcmp(a.path, b.path) < 0;
}

TimeStamp StatCache::GetMtime(const string& path) {
  METRIC_RECORD("cached stat");
  StatCacheData* data = GetView();

  StatCacheEntry value;
  strcpy(value.path, path.c_str());
  StatCacheEntry* end = &data->entries[data->num_entries];
  StatCacheEntry* i = lower_bound(data->entries, end, value, PathCompare);
  if (i == end || strcmp(i->path, path.c_str()) != 0) {
    failed_lookup_paths_.push_back(path);
    return -1;
  }
  return i->mtime;
}

vector<string> StatCache::FinishBuild(bool quiet) {
  data_.Release();

  vector<string> failed_paths_copy = failed_lookup_paths_;
  failed_lookup_paths_.resize(0);
  return failed_paths_copy;
  /*
  int count = failed_lookup_paths_.end() - failed_lookup_paths_.begin();
  if (count > 0) {
    if (!quiet)
      printf("ninja: %d stat cache misses, adding to daemon.\n", count);
    int printed = 0;
    interesting_paths_.StartAdditions();
    for (vector<string>::iterator i(failed_lookup_paths_.begin());
        i != failed_lookup_paths_.end(); ++i) {
      interesting_paths_.Add(*i);
      if (printed < 10) {
        if (!quiet)
          printf("ninja:  %s\n", i->c_str());
      } else if (printed == 10) {
        if (!quiet)
          printf("ninja:  ... more paths elided\n", i->c_str());
      }
      ++printed;
    }
    interesting_paths_.FinishAdditions();
  }

  failed_lookup_paths_.resize(0);
  */
}


void StatCache::StartProcessingChanges() {
  data_.Acquire();
}

void StatCache::EmptyCache() {
  StatCacheData* data = GetView();
  data->num_entries = 0;
}

void StatCache::NotifyChange(const string& path, TimeStamp mtime, bool defer_sort) {
  if (mtime == -1) {
    assert(disk_interface_);
    mtime = disk_interface_->Stat(path);
  }
  StatCacheData* data = GetView();

  // Look up previous entry. If found, then update timestamp.
  StatCacheEntry* end = &data->entries[data->num_entries];
  StatCacheEntry value;
  strcpy(value.path, path.c_str());
  StatCacheEntry* i = lower_bound(data->entries, end, value, StatCachePathCompare);
  if (i != end && strcmp(i->path, path.c_str()) == 0) {
    i->mtime = mtime;
    return;
  }

  // Otherwise append and resort. TODO: It would be nice to defer the sort to
  // the end, with a unique to handle repeated inserts. This would work OK as
  // long as we have a stable sort and did a custom unique that took the last
  // insertion (rather than the first).
  if (data->num_entries >= data->max_entries) {
    Fatal("todo; grow stat cache");
  }
  //printf("NotifyChange: currently %d entries of %d\n", data->num_entries, data->max_entries);
  //printf("  adding %s\n", path.c_str());
  i = &data->entries[data->num_entries++];
  strcpy(i->path, path.c_str());
  i->mtime = mtime;
  if (!defer_sort)
    Sort();
}

void StatCache::Sort() {
  StatCacheData* data = GetView();
  sort(data->entries, &data->entries[data->num_entries], StatCachePathCompare);
}

void StatCache::FinishProcessingChanges() {
  data_.Release();
}

StatCacheData* StatCache::GetView() {
  return reinterpret_cast<StatCacheData*>(data_.View());
}
