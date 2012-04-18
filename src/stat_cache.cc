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
//
// Stores data in 2 files, .ninja_stat_roots, and .ninja_stat_cache.
//
// ninaj_stat_roots list all the directories we care about (for which
// timestamps should be cached). Format of file is 
//
//   int num_roots;
//   char paths[num_roots][_MAX_PATH];
//
// When the roots are modified, the stat_cache will be cleared (and must be
// fully repopulated). So, roots is useful just as a filter for change events
// from the OS.
//
// ninja_stat_cache is conceptually map<path, TimeStamp>, stored as a sorted
// array.
//
//   int num_entries;
//   struct { char path[_MAX_PATH]; int timestamp; } entries;
//
// 

#include "stat_cache.h"

#include "interesting_paths.h"

#include <algorithm>

namespace {

vector<string> gFailedLookupPaths;

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

struct StatCacheData {
  int num_entries;
  int max_entries;
  StatCacheEntry entries[1];
};

static const char* kStatCacheFileName = ".ninja_stat_cache";

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
  StatCache stat_cache(false);
  stat_cache.StartBuild();
  StatCacheData* data = stat_cache.GetView();
  for (int i = 0; i < data->num_entries; ++i) {
    printf("%d: %s -> %d\n", i, data->entries[i].path, data->entries[i].mtime);
  }
  stat_cache.FinishBuild();
}

StatCache::StatCache(bool create) :
    data_(kStatCacheFileName, create),
    interesting_paths_(create) {
  if (data_.ShouldInitialize()) {
    StatCacheData* data = GetView();
    data->num_entries = 0;
    data->max_entries = (data_.Size() - sizeof(StatCacheData)) /
                        sizeof(StatCacheEntry);
  }
}

void StatCache::StartBuild() {
  data_.Acquire();
}

static bool PathCompare(const StatCacheEntry& a, const StatCacheEntry& b) {
  return strcmp(a.path, b.path) < 0;
}

TimeStamp StatCache::GetMtime(const string& path) {
  StatCacheData* data = GetView();

  StatCacheEntry value;
  strcpy(value.path, path.c_str());
  StatCacheEntry* end = &data->entries[data->num_entries];
  StatCacheEntry* i = lower_bound(data->entries, end, value, PathCompare);
  if (i == end || strcmp(i->path, path.c_str()) != 0) {
    gFailedLookupPaths.push_back(path);
    return -1;
  }
  return i->mtime;
}

void StatCache::FinishBuild() {
  data_.Release();

  int count = gFailedLookupPaths.end() - gFailedLookupPaths.begin();
  if (count > 0) {
    printf("ninja: %d stat cache misses, adding to daemon.\n", count);
    interesting_paths_.StartAdditions();
    for (vector<string>::iterator i(gFailedLookupPaths.begin());
        i != gFailedLookupPaths.end(); ++i) {
      interesting_paths_.Add(*i);
    }
    interesting_paths_.FinishAdditions();
  }
}


void StatCache::StartProcessingChanges() {
  data_.Acquire();
  interesting_paths_.StartLookups();
}

bool StatCache::IsInteresting(DWORDLONG parent_index) {
  return interesting_paths_.IsPathInteresting(parent_index);
}

void StatCache::NotifyChange(const string& path, TimeStamp mtime, bool defer_sort) {
  if (mtime == -1)
    mtime = StatPath(path);
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
  interesting_paths_.FinishLookups();
}

bool StatCache::InterestingPathsDirtied(int* num_entries, DWORDLONG** entries) {
  return interesting_paths_.IsDirty(num_entries, entries);
}

void StatCache::ClearInterestingPathsDirtyFlag() {
  interesting_paths_.ClearDirty();
}

StatCacheData* StatCache::GetView() {
  return reinterpret_cast<StatCacheData*>(data_.View());
}

#if 0
#include "graph.h"
#include "hash_map.h"
#include "includes_normalize.h"
#include "metrics.h"
#include "state.h"
#include "util.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <vector>

namespace {
vector<StringPiece> gPaths;
bool gHavePreCached = false;
hash_map<string, TimeStamp> gPreCache;
}

void StatCache::Init() {
  gPaths.reserve(50000);
}

// static
void StatCache::Inform(StringPiece path) {
#ifdef _WIN32
  if (gHavePreCached)
    return;
  //printf("   inform: %*s\n", path.len_, path.str_);
  StringPiece::const_reverse_iterator at =
      std::find(path.rbegin(), path.rend(), '\\');
  if (at != path.rend())
    gPaths.push_back(StringPiece(path.str_, at.base() - path.str_));
#endif
}

// static
void StatCache::PreCache(State* state) {
#ifdef _WIN32
  METRIC_RECORD("statcache precache");
  string root = ""; // We always want to search the root as well.
  gPaths.push_back(root);
  sort(gPaths.begin(), gPaths.end());
  vector<StringPiece>::const_iterator end = unique(gPaths.begin(), gPaths.end());
  for (vector<StringPiece>::const_iterator i = gPaths.begin(); i != end; ++i) {
    WIN32_FIND_DATA find_data;
    string search_root = IncludesNormalize::ToLower(i->AsString());
    string search = search_root + "*";
    //printf("stating: %s\n", search.c_str());
    HANDLE handle = FindFirstFile(search.c_str(), &find_data);
    if (handle == INVALID_HANDLE_VALUE)
      continue;
    for (;;) {
      if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        string name = search_root +
                      IncludesNormalize::ToLower(find_data.cFileName);
        //string err;
        //if (!CanonicalizePath(&name, &err))
          //continue;
        //printf("  cache for %s\n", name.c_str());
        gPreCache[name] = FiletimeToTimestamp(find_data.ftLastWriteTime);
      }
      BOOL success = FindNextFile(handle, &find_data);
      if (!success)
        break;
    }
    FindClose(handle);
  }
  gHavePreCached = true;
#endif
}

TimeStamp StatCache::GetMtime(const string& path) {
  printf("gPreCache: %d\n", gPreCache.size());
  exit(-1);
  hash_map<string, TimeStamp>::iterator i = gPreCache.find(path);
  TimeStamp ret = -1;
  if (i != gPreCache.end()) {
    ret = i->second;
    //i->second = -1;
  }
  //printf("Mtime: %s: %d\n", path.c_str(), ret);
  return ret;
}
#endif
