// Copyright 2011 Google Inc. All Rights Reserved.
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

#ifndef NINJA_BUILD_LOG_H_
#define NINJA_BUILD_LOG_H_

#include <string>
#include <stdio.h>
using namespace std;

#include "hash_map.h"
#include "timestamp.h"
#include "util.h"  // uint64_t

struct Edge;

/// Can answer questions about the manifest for the BuildLog.
struct BuildLogUser {
  /// Return if a given output no longer part of the build manifest.
  /// This is only called during recompaction and doesn't have to be fast.
  virtual bool IsPathDead(StringPiece s) const = 0;
};

/// Store a log of every command ran for every build.
/// It has a few uses:
///
/// 1) (hashes of) command lines for existing output files, so we know
///    when we need to rebuild due to the command changing
/// 2) timing information, perhaps for generating reports
/// 3) restat information
struct BuildLog {
  BuildLog();
  ~BuildLog();

  bool OpenForWrite(const string& path, const BuildLogUser& user, string* err);
  bool RecordCommand(Edge* edge, int start_time, int end_time,
                     TimeStamp restat_mtime = 0);
  void Close();

  /// Load the on-disk log.
  bool Load(const string& path, string* err);

  struct LogEntry {
    string output;
    uint64_t command_hash;
    int start_time;
    int end_time;
    TimeStamp restat_mtime;

    static uint64_t HashCommand(StringPiece command);

    // Used by tests.
    bool operator==(const LogEntry& o) {
      return output == o.output && command_hash == o.command_hash &&
          start_time == o.start_time && end_time == o.end_time &&
          restat_mtime == o.restat_mtime;
    }

    explicit LogEntry(const string& output);
    LogEntry(const string& output, uint64_t command_hash,
             int start_time, int end_time, TimeStamp restat_mtime);
  };

  /// Lookup a previously-run command by its output path.
  LogEntry* LookupByOutput(const string& path);

  /// Serialize an entry into a log file.
  bool WriteEntry(FILE* f, const LogEntry& entry);

  /// Rewrite the known log entries, throwing away old data.
  bool Recompact(const string& path, const BuildLogUser& user, string* err);

  typedef ExternalStringHashMap<LogEntry*>::Type Entries;
  const Entries& entries() const { return entries_; }

 private:
  Entries entries_;
  FILE* log_file_;
  bool needs_recompaction_;
};

#endif // NINJA_BUILD_LOG_H_
