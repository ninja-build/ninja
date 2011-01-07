#include "build_log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ninja.h"

// Implementation details:
// Each run's log appends to the log file.
// To load, we run through all log entries in series, throwing away
// older runs.
// XXX figure out recompaction strategy

bool BuildLog::OpenForWrite(const string& path, string* err) {
  log_file_ = fopen(path.c_str(), "ab");
  if (!log_file_) {
    *err = strerror(errno);
    return false;
  }
  setlinebuf(log_file_);
  return true;
}

void BuildLog::RecordCommand(Edge* edge, int time_ms) {
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

    fprintf(log_file_, "%d %s %s\n", time_ms, path.c_str(), command.c_str());
  }
}

void BuildLog::Close() {
  fclose(log_file_);
  log_file_ = NULL;
}

// Load the on-disk log.
bool BuildLog::Load(const string& path, string* err) {
  FILE* file = fopen(path.c_str(), "r");
  if (!file) {
    if (errno == ENOENT)
      return true;
    *err = strerror(errno);
    return false;
  }

  char buf[256 << 10];
  while (fgets(buf, sizeof(buf), file)) {
    char* start = buf;
    char* end = strchr(start, ' ');
    if (!end)
      continue;

    LogEntry* entry = new LogEntry;
    *end = 0;
    entry->time_ms = atoi(start);

    start = end + 1;
    end = strchr(start, ' ');
    entry->output = string(start, end - start);

    start = end + 1;
    end = strchr(start, '\n');
    entry->command = string(start, end - start);
    log_[entry->output] = entry;
  }

  return true;
}

// Lookup a previously-run command by its output path.
BuildLog::LogEntry* BuildLog::LookupByOutput(const string& path) {
  Log::iterator i = log_.find(path);
  if (i != log_.end())
    return i->second;
  return NULL;
}
