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
#include <string.h>

#include "build.h"
#include "graph.h"
#include "ninja.h"

// Implementation details:
// Each run's log appends to the log file.
// To load, we run through all log entries in series, throwing away
// older runs.
// Once the number of redundant entries exceeds a threshold, we write
// out a new file and replace the existing one with it.

BuildLog::BuildLog()
  : log_file_(NULL), config_(NULL), needs_recompaction_(false) {}

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
  setlinebuf(log_file_);
  return true;
}

void BuildLog::RecordCommand(Edge* edge, int time_ms) {
  if (!log_file_)
    return;

  const string command = edge->EvaluateCommand();
  for (vector<Node*>::iterator out = edge->outputs_.begin();
       out != edge->outputs_.end(); ++out) {
    const string& path = (*out)->file_->path_;
    Log::iterator i = log_.find(path);
    LogEntry* log_entry;
    if (i != log_.end()) {
      log_entry = i->second;
    } else {
      log_entry = new LogEntry;
      log_.insert(make_pair(path, log_entry));
    }
    log_entry->output = path;
    log_entry->command = command;
    log_entry->time_ms = time_ms;

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
  log_file_ = file; // record this to close it later, as Windows needs this
  if (!file) {
    if (errno == ENOENT)
      return true;
    *err = strerror(errno);
    return false;
  }

  int unique_entry_count = 0;
  int total_entry_count = 0;

  char buf[256 << 10];
  while (fgets(buf, sizeof(buf), file)) {
    char* start = buf;
    char* end = strchr(start, ' ');
    if (!end)
      continue;

    *end = 0;
    int time_ms = atoi(start);
    start = end + 1;
    end = strchr(start, ' ');
    string output = string(start, end - start);

    LogEntry* entry;
    Log::iterator i = log_.find(output);
    if (i != log_.end()) {
      entry = i->second;
    } else {
      entry = new LogEntry;
      log_.insert(make_pair(output, entry));
      ++unique_entry_count;
    }
    ++total_entry_count;

    entry->time_ms = time_ms;
    entry->output = output;

    start = end + 1;
    end = strchr(start, '\n');
    entry->command = string(start, end - start);
  }

  // Mark the log as "needs rebuiding" if it has kCompactionRatio times
  // too many log entries.
  int kCompactionRatio = 3;
  if (total_entry_count > unique_entry_count * kCompactionRatio)
    needs_recompaction_ = true;

  fclose(file);

  return true;
}

BuildLog::LogEntry* BuildLog::LookupByOutput(const string& path) {
  Log::iterator i = log_.find(path);
  if (i != log_.end())
    return i->second;
  return NULL;
}

void BuildLog::WriteEntry(FILE* f, const LogEntry& entry) {
  fprintf(f, "%d %s %s\n",
          entry.time_ms, entry.output.c_str(), entry.command.c_str());
}

bool BuildLog::Recompact(const string& path, string* err) {
  printf("Recompacting log...\n");

  string temp_path = path + ".recompact";
  FILE* f = fopen(temp_path.c_str(), "wb");
  if (!f) {
    *err = strerror(errno);
    return false;
  }

  for (Log::iterator i = log_.begin(); i != log_.end(); ++i) {
    WriteEntry(f, *i->second);
  }

  // Windows needs to close and unlink a file before a rename will be permitted
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
