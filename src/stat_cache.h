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

#ifndef NINJA_STAT_CACHE_H_
#define NINJA_STAT_CACHE_H_

#include "lockable_mapped_file.h"
#include "state.h"
#include "string_piece.h"
#include "util.h"

#include <vector>
#include <string>

struct DiskInterface;

struct StatCache {
  StatCache(bool create,
            DiskInterface* disk_interface);

  // Has the stat cache been globally enabled (via NINJA_STAT_DAEMON env var)?
  static bool Active();

  //
  // From ninja side
  //

  // Acquires lock (stops daemon from updating), and makes GetMtime valid.
  // Must be paired with EndBuild.
  void StartBuild();

  // Retrieve cached time stamp for given path. -1 is "unavailable", 0 is
  // "does not exist", > 0 is timestamp. -1s are considered failures, and
  // they'll be added to the interesting-paths-set for the daemon.
  TimeStamp GetMtime(const string& path);

  // Releases lock, returns list of paths that should be added to interesting
  // paths (paths to watch).
  vector<string> FinishBuild(bool quiet = false);

  // Some utilities that should probably be in ninja proper instead.
  static void EnsureDaemonRunning();
  static void Dump();
  static void ValidateAgainstDisk(DiskInterface& disk_interface);

  //
  // From daemon side
  //

  void StartProcessingChanges();
  void EmptyCache();
  void NotifyChange(const string& path, TimeStamp timestamp, bool defer_sort);
  void Sort();

  void FinishProcessingChanges();

private:
  struct StatCacheData* GetView();

  LockableMappedFile data_;
  DiskInterface* disk_interface_;
  vector<string> failed_lookup_paths_;

  static int is_active_;
};

#endif  // NINJA_STAT_CACHE_H_
