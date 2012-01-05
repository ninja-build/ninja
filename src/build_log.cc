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

#include "build_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "graph.h"
#include "util.h"

// Implementation details:
// Each run's log appends to the log file.
// To load, we run through all log entries in series, throwing away
// older runs.
// Once the number of redundant entries exceeds a threshold, we write
// out a new file and replace the existing one with it.

namespace {

const char kFileSignature[] = "# ninja log v%d\n";
const int kCurrentVersion = 4;

}  // namespace

BuildLog::BuildLog()
  : log_file_(NULL), config_(NULL), needs_recompaction_(false) {}

BuildLog::~BuildLog() {
  Close();
}

bool BuildLog::OpenForWrite(const string& path, string* err) {
  if (config_ && config_->dry_run)
    return true;  // Do nothing, report success.

  if (needs_recompaction_) {
    Close();
    if (!Recompact(path, err))
      return false;
  }

  log_file_ = fopen(path.c_str(), "ab");
  if (!log_file_) {
    *err = strerror(errno);
    return false;
  }
  setvbuf(log_file_, NULL, _IOLBF, BUFSIZ);
  SetCloseOnExec(fileno(log_file_));

  if (ftell(log_file_) == 0) {
    if (fprintf(log_file_, kFileSignature, kCurrentVersion) < 0) {
      *err = strerror(errno);
      return false;
    }
  }

  return true;
}

void BuildLog::RecordCommand(Edge* edge, int start_time, int end_time,
                             time_t restat_mtime) {
  const string command = edge->EvaluateCommand();
  for (vector<Node*>::iterator out = edge->outputs_.begin();
       out != edge->outputs_.end(); ++out) {
    const string& path = (*out)->path();
    Log::iterator i = log_.find(path.c_str());
    LogEntry* log_entry;
    if (i != log_.end()) {
      log_entry = i->second;
    } else {
      log_entry = new LogEntry;
      log_entry->output = path;
      log_.insert(make_pair(log_entry->output.c_str(), log_entry));
    }
    log_entry->command = command;
    log_entry->start_time = start_time;
    log_entry->end_time = end_time;
    log_entry->restat_mtime = restat_mtime;

    if (log_file_)
      WriteEntry(log_file_, *log_entry);
  }
}

void BuildLog::Close() {
  if (log_file_)
    fclose(log_file_);
  log_file_ = NULL;
}

bool BuildLog::Load(const string& path, string* err) {
  FILE* file = fopen(path.c_str(), "r");
  if (!file) {
    if (errno == ENOENT)
      return true;
    *err = strerror(errno);
    return false;
  }

  int log_version = 0;
  int unique_entry_count = 0;
  int total_entry_count = 0;

  char buf[256 << 10];
  while (fgets(buf, sizeof(buf), file)) {
    if (!log_version) {
      log_version = 1;  // Assume by default.
      if (sscanf(buf, kFileSignature, &log_version) > 0)
        continue;
    }

    char field_separator = log_version >= 4 ? '\t' : ' ';

    char* start = buf;
    char* end = strchr(start, field_separator);
    if (!end)
      continue;
    *end = 0;

    int start_time = 0, end_time = 0;
    time_t restat_mtime = 0;

    if (log_version == 1) {
      // In v1 we logged how long the command took; we don't use this info.
      // int time_ms = atoi(start);
      start = end + 1;
    } else {
      // In v2 we log the start time and the end time.
      start_time = atoi(start);
      start = end + 1;

      char* end = strchr(start, field_separator);
      if (!end)
        continue;
      *end = 0;
      end_time = atoi(start);
      start = end + 1;
    }

    if (log_version >= 3) {
      // In v3 we log the restat mtime.
      char* end = strchr(start, field_separator);
      if (!end)
        continue;
      *end = 0;
      restat_mtime = atol(start);
      start = end + 1;
    }

    end = strchr(start, field_separator);
    if (!end)
      continue;
    string output = string(start, end - start);

    start = end + 1;
    end = strchr(start, '\n');
    if (!end)
      continue;

    LogEntry* entry;
    Log::iterator i = log_.find(output.c_str());
    if (i != log_.end()) {
      entry = i->second;
    } else {
      entry = new LogEntry;
      entry->output = output;
      log_.insert(make_pair(entry->output.c_str(), entry));
      ++unique_entry_count;
    }
    ++total_entry_count;

    entry->start_time = start_time;
    entry->end_time = end_time;
    entry->restat_mtime = restat_mtime;
    entry->command = string(start, end - start);
  }

  // Decide whether it's time to rebuild the log:
  // - if we're upgrading versions
  // - if it's getting large
  int kMinCompactionEntryCount = 100;
  int kCompactionRatio = 3;
  if (log_version < kCurrentVersion) {
    needs_recompaction_ = true;
  } else if (total_entry_count > kMinCompactionEntryCount &&
             total_entry_count > unique_entry_count * kCompactionRatio) {
    needs_recompaction_ = true;
  }

  fclose(file);

  return true;
}

BuildLog::LogEntry* BuildLog::LookupByOutput(const string& path) {
  Log::iterator i = log_.find(path.c_str());
  if (i != log_.end())
    return i->second;
  return NULL;
}

void BuildLog::WriteEntry(FILE* f, const LogEntry& entry) {
  fprintf(f, "%d\t%d\t%ld\t%s\t%s\n",
          entry.start_time, entry.end_time, (long) entry.restat_mtime,
          entry.output.c_str(), entry.command.c_str());
}

bool BuildLog::Recompact(const string& path, string* err) {
  printf("Recompacting log...\n");

  string temp_path = path + ".recompact";
  FILE* f = fopen(temp_path.c_str(), "wb");
  if (!f) {
    *err = strerror(errno);
    return false;
  }

  if (fprintf(f, kFileSignature, kCurrentVersion) < 0) {
    *err = strerror(errno);
    return false;
  }

  for (Log::iterator i = log_.begin(); i != log_.end(); ++i) {
    WriteEntry(f, *i->second);
  }

  fclose(f);
  if (unlink(path.c_str()) < 0) {
    *err = strerror(errno);
    return false;
  }

  if (rename(temp_path.c_str(), path.c_str()) < 0) {
    *err = strerror(errno);
    return false;
  }

  return true;
}
