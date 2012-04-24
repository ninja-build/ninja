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

#include "interesting_paths.h"
#include "lockable_mapped_file.h"
#include "state.h"
#include "string_piece.h"
#include "util.h"

struct StatCache {
  StatCache(bool create, InterestingPaths& interesting_paths);

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

  // Updates roots based on paths it was Inform'd about, releases lock so
  // daemon can resume timestamp updates.
  void FinishBuild();

  // Some utilities that should probably be in ninja proper instead.
  static void EnsureDaemonRunning();
  static void Dump();
  static void ValidateAgainstDisk();

  //
  // From daemon side
  //

  void StartProcessingChanges();

  bool IsInteresting(DWORDLONG parent_index);
  void EmptyCache();
  void NotifyChange(const string& path, TimeStamp timestamp, bool defer_sort);
  void Sort();
  bool InterestingPathsDirtied(int* num_entries, DWORDLONG** entries);
  void ClearInterestingPathsDirtyFlag();

  void FinishProcessingChanges();

  void CheckForInterestingPathsDirtied();



private:
  struct StatCacheData* GetView();

  InterestingPaths& interesting_paths_;
  LockableMappedFile data_;

  static int is_active_;
};

#endif  // NINJA_STAT_CACHE_H_
