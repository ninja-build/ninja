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

// On AIX, inttypes.h gets indirectly included by build_log.h.
// It's easiest just to ask for the printf format macros right away.
#ifndef _WIN32
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#endif

#include "build_log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <inttypes.h>
#include <unistd.h>
#endif

#include "build.h"
#include "graph.h"
#include "metrics.h"
#include "util.h"

// Implementation details:
// Each run's log appends to the log file.
// To load, we run through all log entries in series, throwing away
// older runs.
// Once the number of redundant entries exceeds a threshold, we write
// out a new file and replace the existing one with it.

namespace {

const char kFileSignature[] = "# ninja log v%d\n";
const int kOldestSupportedVersion = 4;
const int kCurrentVersion = 5;

// 64bit MurmurHash2, by Austin Appleby
#if defined(_MSC_VER)
#define BIG_CONSTANT(x) (x)
#else   // defined(_MSC_VER)
#define BIG_CONSTANT(x) (x##LLU)
#endif // !defined(_MSC_VER)
inline
uint64_t MurmurHash64A(const void* key, size_t len) {
  static const uint64_t seed = 0xDECAFBADDECAFBADull;
  const uint64_t m = BIG_CONSTANT(0xc6a4a7935bd1e995);
  const int r = 47;
  uint64_t h = seed ^ (len * m);
  const unsigned char* data = (const unsigned char*)key;
  while (len >= 8) {
    uint64_t k;
    memcpy(&k, data, sizeof k);
    k *= m;
    k ^= k >> r;
    k *= m;
    h ^= k;
    h *= m;
    data += 8;
    len -= 8;
  }
  switch (len & 7)
  {
  case 7: h ^= uint64_t(data[6]) << 48;
  case 6: h ^= uint64_t(data[5]) << 40;
  case 5: h ^= uint64_t(data[4]) << 32;
  case 4: h ^= uint64_t(data[3]) << 24;
  case 3: h ^= uint64_t(data[2]) << 16;
  case 2: h ^= uint64_t(data[1]) << 8;
  case 1: h ^= uint64_t(data[0]);
          h *= m;
  };
  h ^= h >> r;
  h *= m;
  h ^= h >> r;
  return h;
}
#undef BIG_CONSTANT


}  // namespace

// static
uint64_t BuildLog::LogEntry::HashCommand(StringPiece command) {
  return MurmurHash64A(command.str_, command.len_);
}

BuildLog::LogEntry::LogEntry(const string& output)
  : output(output) {}

BuildLog::LogEntry::LogEntry(const string& output, uint64_t command_hash,
  int start_time, int end_time, TimeStamp restat_mtime)
  : output(output), command_hash(command_hash),
    start_time(start_time), end_time(end_time), mtime(restat_mtime)
{}

BuildLog::BuildLog()
  : log_file_(NULL), needs_recompaction_(false) {}

BuildLog::~BuildLog() {
  Close();
}

bool BuildLog::OpenForWrite(const string& path, const BuildLogUser& user,
                            string* err) {
  if (needs_recompaction_) {
    if (!Recompact(path, user, err))
      return false;
  }

  log_file_ = fopen(path.c_str(), "ab");
  if (!log_file_) {
    *err = strerror(errno);
    return false;
  }
  setvbuf(log_file_, NULL, _IOLBF, BUFSIZ);
  SetCloseOnExec(fileno(log_file_));

  // Opening a file in append mode doesn't set the file pointer to the file's
  // end on Windows. Do that explicitly.
  fseek(log_file_, 0, SEEK_END);

  if (ftell(log_file_) == 0) {
    if (fprintf(log_file_, kFileSignature, kCurrentVersion) < 0) {
      *err = strerror(errno);
      return false;
    }
  }

  return true;
}

bool BuildLog::RecordCommand(Edge* edge, int start_time, int end_time,
                             TimeStamp mtime) {
  string command = edge->EvaluateCommand(true);
  uint64_t command_hash = LogEntry::HashCommand(command);
  for (vector<Node*>::iterator out = edge->outputs_.begin();
       out != edge->outputs_.end(); ++out) {
    const string& path = (*out)->path();
    Entries::iterator i = entries_.find(path);
    LogEntry* log_entry;
    if (i != entries_.end()) {
      log_entry = i->second;
    } else {
      log_entry = new LogEntry(path);
      entries_.insert(Entries::value_type(log_entry->output, log_entry));
    }
    log_entry->command_hash = command_hash;
    log_entry->start_time = start_time;
    log_entry->end_time = end_time;
    log_entry->mtime = mtime;

    if (log_file_) {
      if (!WriteEntry(log_file_, *log_entry))
        return false;
    }
  }
  return true;
}

void BuildLog::Close() {
  if (log_file_)
    fclose(log_file_);
  log_file_ = NULL;
}

struct LineReader {
  explicit LineReader(FILE* file)
    : file_(file), buf_end_(buf_), line_start_(buf_), line_end_(NULL) {
      memset(buf_, 0, sizeof(buf_));
  }

  // Reads a \n-terminated line from the file passed to the constructor.
  // On return, *line_start points to the beginning of the next line, and
  // *line_end points to the \n at the end of the line. If no newline is seen
  // in a fixed buffer size, *line_end is set to NULL. Returns false on EOF.
  bool ReadLine(char** line_start, char** line_end) {
    if (line_start_ >= buf_end_ || !line_end_) {
      // Buffer empty, refill.
      size_t size_read = fread(buf_, 1, sizeof(buf_), file_);
      if (!size_read)
        return false;
      line_start_ = buf_;
      buf_end_ = buf_ + size_read;
    } else {
      // Advance to next line in buffer.
      line_start_ = line_end_ + 1;
    }

    line_end_ = (char*)memchr(line_start_, '\n', buf_end_ - line_start_);
    if (!line_end_) {
      // No newline. Move rest of data to start of buffer, fill rest.
      size_t already_consumed = line_start_ - buf_;
      size_t size_rest = (buf_end_ - buf_) - already_consumed;
      memmove(buf_, line_start_, size_rest);

      size_t read = fread(buf_ + size_rest, 1, sizeof(buf_) - size_rest, file_);
      buf_end_ = buf_ + size_rest + read;
      line_start_ = buf_;
      line_end_ = (char*)memchr(line_start_, '\n', buf_end_ - line_start_);
    }

    *line_start = line_start_;
    *line_end = line_end_;
    return true;
  }

 private:
  FILE* file_;
  char buf_[256 << 10];
  char* buf_end_;  // Points one past the last valid byte in |buf_|.

  char* line_start_;
  // Points at the next \n in buf_ after line_start, or NULL.
  char* line_end_;
};

bool BuildLog::Load(const string& path, string* err) {
  METRIC_RECORD(".ninja_log load");
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

  LineReader reader(file);
  char* line_start = 0;
  char* line_end = 0;
  while (reader.ReadLine(&line_start, &line_end)) {
    if (!log_version) {
      sscanf(line_start, kFileSignature, &log_version);

      if (log_version < kOldestSupportedVersion) {
        *err = ("build log version invalid, perhaps due to being too old; "
                "starting over");
        fclose(file);
        unlink(path.c_str());
        // Don't report this as a failure.  An empty build log will cause
        // us to rebuild the outputs anyway.
        return true;
      }
    }

    // If no newline was found in this chunk, read the next.
    if (!line_end)
      continue;

    const char kFieldSeparator = '\t';

    char* start = line_start;
    char* end = (char*)memchr(start, kFieldSeparator, line_end - start);
    if (!end)
      continue;
    *end = 0;

    int start_time = 0, end_time = 0;
    TimeStamp restat_mtime = 0;

    start_time = atoi(start);
    start = end + 1;

    end = (char*)memchr(start, kFieldSeparator, line_end - start);
    if (!end)
      continue;
    *end = 0;
    end_time = atoi(start);
    start = end + 1;

    end = (char*)memchr(start, kFieldSeparator, line_end - start);
    if (!end)
      continue;
    *end = 0;
    restat_mtime = atol(start);
    start = end + 1;

    end = (char*)memchr(start, kFieldSeparator, line_end - start);
    if (!end)
      continue;
    string output = string(start, end - start);

    start = end + 1;
    end = line_end;

    LogEntry* entry;
    Entries::iterator i = entries_.find(output);
    if (i != entries_.end()) {
      entry = i->second;
    } else {
      entry = new LogEntry(output);
      entries_.insert(Entries::value_type(entry->output, entry));
      ++unique_entry_count;
    }
    ++total_entry_count;

    entry->start_time = start_time;
    entry->end_time = end_time;
    entry->mtime = restat_mtime;
    if (log_version >= 5) {
      char c = *end; *end = '\0';
      entry->command_hash = (uint64_t)strtoull(start, NULL, 16);
      *end = c;
    } else {
      entry->command_hash = LogEntry::HashCommand(StringPiece(start,
                                                              end - start));
    }
  }
  fclose(file);

  if (!line_start) {
    return true; // file was empty
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

  return true;
}

BuildLog::LogEntry* BuildLog::LookupByOutput(const string& path) {
  Entries::iterator i = entries_.find(path);
  if (i != entries_.end())
    return i->second;
  return NULL;
}

bool BuildLog::WriteEntry(FILE* f, const LogEntry& entry) {
  return fprintf(f, "%d\t%d\t%d\t%s\t%" PRIx64 "\n",
          entry.start_time, entry.end_time, entry.mtime,
          entry.output.c_str(), entry.command_hash) > 0;
}

bool BuildLog::Recompact(const string& path, const BuildLogUser& user,
                         string* err) {
  METRIC_RECORD(".ninja_log recompact");

  Close();
  string temp_path = path + ".recompact";
  FILE* f = fopen(temp_path.c_str(), "wb");
  if (!f) {
    *err = strerror(errno);
    return false;
  }

  if (fprintf(f, kFileSignature, kCurrentVersion) < 0) {
    *err = strerror(errno);
    fclose(f);
    return false;
  }

  vector<StringPiece> dead_outputs;
  for (Entries::iterator i = entries_.begin(); i != entries_.end(); ++i) {
    if (user.IsPathDead(i->first)) {
      dead_outputs.push_back(i->first);
      continue;
    }

    if (!WriteEntry(f, *i->second)) {
      *err = strerror(errno);
      fclose(f);
      return false;
    }
  }

  for (size_t i = 0; i < dead_outputs.size(); ++i)
    entries_.erase(dead_outputs[i]);

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
