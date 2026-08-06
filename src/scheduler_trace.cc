// Copyright 2026 Google Inc. All Rights Reserved.
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

#include "scheduler_trace.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_map>

#include "graph.h"
#include "state.h"

using namespace std;

namespace {

const char kTraceMagic[] = "ninja_scheduler_trace";
const char kCausalSemantics[] = "causal-v2";
const char kLegacySemantics[] = "global-v1";

SchedulerTraceOperationCounts g_operation_counts;

string Uint64ToString(uint64_t value) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%llu",
           static_cast<unsigned long long>(value));
  return buffer;
}

string IntToString(int value) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%d", value);
  return buffer;
}

bool IsFieldName(const string& name) {
  if (name.empty())
    return false;
  for (char c : name) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
        c == '-') {
      continue;
    }
    return false;
  }
  return true;
}

char HexDigit(unsigned int value) {
  return "0123456789abcdef"[value & 0xf];
}

int HexValue(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

string EncodeField(const string& value) {
  string result;
  result.reserve(value.size());
  for (unsigned char c : value) {
    if (c == '%' || c == '\t' || c == '\n' || c == '\r' || c < 0x20 ||
        c == 0x7f) {
      result.push_back('%');
      result.push_back(HexDigit(c >> 4));
      result.push_back(HexDigit(c));
    } else {
      result.push_back(static_cast<char>(c));
    }
  }
  return result;
}

bool DecodeField(const string& value, string* result) {
  result->clear();
  result->reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] != '%') {
      result->push_back(value[i]);
      continue;
    }
    if (i + 2 >= value.size())
      return false;
    int high = HexValue(value[i + 1]);
    int low = HexValue(value[i + 2]);
    if (high < 0 || low < 0)
      return false;
    result->push_back(static_cast<char>((high << 4) | low));
    i += 2;
  }
  return true;
}

void AppendField(string* line, const string& name, const string& value) {
  line->push_back('\t');
  line->append(name);
  line->push_back('=');
  line->append(EncodeField(value));
}

class StableHasher {
 public:
  StableHasher()
      : first_(UINT64_C(1469598103934665603)),
        second_(UINT64_C(1099511628211)) {}

  void Add(const string& value) {
    AddNumber(value.size());
    for (unsigned char c : value) {
      first_ ^= c;
      first_ *= UINT64_C(1099511628211);
      second_ ^= static_cast<unsigned char>(c + 0x9d);
      second_ *= UINT64_C(14029467366897019727);
      second_ ^= second_ >> 29;
    }
  }

  string Hex128() const {
    char result[33];
    snprintf(result, sizeof(result), "%016llx%016llx",
             static_cast<unsigned long long>(first_),
             static_cast<unsigned long long>(second_));
    return result;
  }

  string Hex64() const {
    char result[17];
    snprintf(result, sizeof(result), "%016llx",
             static_cast<unsigned long long>(first_));
    return result;
  }

 private:
  void AddNumber(size_t value) {
    uint64_t fixed_width_value = static_cast<uint64_t>(value);
    for (size_t i = 0; i < sizeof(fixed_width_value); ++i) {
      unsigned char c = static_cast<unsigned char>(fixed_width_value & 0xff);
      first_ ^= c;
      first_ *= UINT64_C(1099511628211);
      second_ ^= static_cast<unsigned char>(c + 0x4f);
      second_ *= UINT64_C(14029467366897019727);
      second_ ^= second_ >> 31;
      fixed_width_value >>= 8;
    }
  }

  uint64_t first_;
  uint64_t second_;
};

string PoolName(const Edge* edge) {
  if (!edge || !edge->pool())
    return string();
  return edge->pool()->name().empty() ? "default" : edge->pool()->name();
}

struct ParsedEvent {
  uint64_t sequence = 0;
  string type;
  SchedulerTraceFields fields;
};

struct ParsedTrace {
  int version = 0;
  SchedulerTraceFields header;
  vector<ParsedEvent> events;
  SchedulerTraceFields terminal;
};

const string* FindField(const SchedulerTraceFields& fields,
                        const string& name) {
  for (const SchedulerTraceField& field : fields) {
    if (field.first == name)
      return &field.second;
  }
  return nullptr;
}

string FieldOrEmpty(const SchedulerTraceFields& fields, const string& name) {
  const string* value = FindField(fields, name);
  return value ? *value : string();
}

bool ParseUnsigned(const string& text, uint64_t* value) {
  if (text.empty() || text[0] == '-')
    return false;
  char* end = nullptr;
  errno = 0;
  unsigned long long parsed = strtoull(text.c_str(), &end, 10);
  if (errno || !end || *end)
    return false;
  *value = static_cast<uint64_t>(parsed);
  return true;
}

bool ParseSigned(const string& text, int* value) {
  if (text.empty())
    return false;
  char* end = nullptr;
  errno = 0;
  long parsed = strtol(text.c_str(), &end, 10);
  if (errno || !end || *end || parsed < numeric_limits<int>::min() ||
      parsed > numeric_limits<int>::max()) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

bool ParseFields(const string& line, string* record_type,
                 SchedulerTraceFields* fields, string* err) {
  fields->clear();
  set<string> field_names;
  size_t start = 0;
  size_t tab = line.find('\t');
  *record_type = line.substr(0, tab);
  if (record_type->empty()) {
    *err = "empty scheduler trace record";
    return false;
  }
  if (tab == string::npos)
    return true;
  start = tab + 1;
  while (start <= line.size()) {
    tab = line.find('\t', start);
    string item =
        line.substr(start, tab == string::npos ? string::npos : tab - start);
    size_t equals = item.find('=');
    if (equals == string::npos || equals == 0) {
      *err = "malformed scheduler trace field '" + item + "'";
      return false;
    }
    string name = item.substr(0, equals);
    if (!IsFieldName(name)) {
      *err = "invalid scheduler trace field name '" + name + "'";
      return false;
    }
    if (!field_names.insert(name).second) {
      *err = "duplicate scheduler trace field '" + name + "'";
      return false;
    }
    string value;
    if (!DecodeField(item.substr(equals + 1), &value)) {
      *err = "invalid percent escape in scheduler trace field '" + name + "'";
      return false;
    }
    fields->push_back(make_pair(name, value));
    if (tab == string::npos)
      break;
    start = tab + 1;
    if (start == line.size()) {
      *err = "empty trailing scheduler trace field";
      return false;
    }
  }
  return true;
}

bool ReadTrace(const string& path, size_t max_events, uint64_t max_bytes,
               ParsedTrace* trace, string* err) {
  FILE* file = fopen(path.c_str(), "rb");
  if (!file) {
    *err = "opening scheduler trace '" + path + "': " + strerror(errno);
    return false;
  }

  string line;
  uint64_t bytes = 0;
  bool saw_header = false;
  bool saw_terminal = false;
  uint64_t expected_sequence = 1;
  size_t line_number = 0;
  bool result = true;

  auto parse_line = [&](const string& input) -> bool {
    ++line_number;
    if (input.empty()) {
      *err =
          "scheduler trace line " + Uint64ToString(line_number) + " is empty";
      return false;
    }
    string record_type;
    SchedulerTraceFields fields;
    string parse_err;
    if (!ParseFields(input, &record_type, &fields, &parse_err)) {
      *err = "scheduler trace line " + Uint64ToString(line_number) + ": " +
             parse_err;
      return false;
    }
    if (!saw_header) {
      if (record_type != kTraceMagic) {
        *err = "scheduler trace is missing its versioned header";
        return false;
      }
      const string* version_text = FindField(fields, "version");
      int version;
      if (!version_text || !ParseSigned(*version_text, &version) ||
          (version != 1 && version != 2)) {
        *err = "unsupported scheduler trace version '" +
               (version_text ? *version_text : string("<missing>")) + "'";
        return false;
      }
      const string required = FieldOrEmpty(fields, "requires");
      const string expected =
          version == 1 ? kLegacySemantics : kCausalSemantics;
      if (required != expected) {
        *err = "scheduler trace requires unknown replay semantics '" +
               (required.empty() ? string("<missing>") : required) + "'";
        return false;
      }
      trace->version = version;
      trace->header = fields;
      saw_header = true;
      return true;
    }
    if (saw_terminal) {
      *err = "scheduler trace has data after its terminal record";
      return false;
    }
    if (record_type == "event") {
      if (trace->events.size() >= max_events) {
        *err = "scheduler trace exceeds the configured event limit of " +
               Uint64ToString(max_events);
        return false;
      }
      const string* sequence_text = FindField(fields, "seq");
      const string* event_type = FindField(fields, "type");
      uint64_t sequence;
      if (!sequence_text || !ParseUnsigned(*sequence_text, &sequence)) {
        *err = "scheduler trace event has an invalid sequence number";
        return false;
      }
      if (sequence != expected_sequence) {
        *err = "scheduler trace event sequence is " + Uint64ToString(sequence) +
               ", expected " + Uint64ToString(expected_sequence);
        return false;
      }
      if (!event_type || event_type->empty()) {
        *err = "scheduler trace event " + Uint64ToString(sequence) +
               " has no type";
        return false;
      }
      ParsedEvent event;
      event.sequence = sequence;
      event.type = *event_type;
      event.fields = std::move(fields);
      trace->events.push_back(std::move(event));
      ++expected_sequence;
      return true;
    }
    if (record_type == "end") {
      const string* count_text = FindField(fields, "events");
      const string* status = FindField(fields, "status");
      uint64_t count;
      if (!count_text || !ParseUnsigned(*count_text, &count) ||
          count != trace->events.size()) {
        *err =
            "scheduler trace terminal event count does not match the "
            "stream";
        return false;
      }
      if (!status || status->empty()) {
        *err = "scheduler trace terminal record has no status";
        return false;
      }
      trace->terminal = fields;
      saw_terminal = true;
      return true;
    }
    *err = "unknown scheduler trace record type '" + record_type + "'";
    return false;
  };

  while (result) {
    int c = fgetc(file);
    if (c == EOF)
      break;
    ++bytes;
    if (bytes > max_bytes) {
      *err = "scheduler trace exceeds the configured byte limit of " +
             Uint64ToString(max_bytes);
      result = false;
      break;
    }
    if (c == '\n') {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      result = parse_line(line);
      line.clear();
      continue;
    }
    if (line.size() >= SchedulerTrace::kMaxLineBytes) {
      *err = "scheduler trace line exceeds the safety limit of " +
             Uint64ToString(SchedulerTrace::kMaxLineBytes) + " bytes";
      result = false;
      break;
    }
    line.push_back(static_cast<char>(c));
  }
  if (result && ferror(file)) {
    *err = "reading scheduler trace '" + path + "': " + strerror(errno);
    result = false;
  }
  if (result && !line.empty()) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    result = parse_line(line);
  }
  fclose(file);

  if (result && !saw_header) {
    *err = "scheduler trace is empty";
    result = false;
  }
  if (result && !saw_terminal) {
    *err = "scheduler trace is truncated: terminal record is missing";
    result = false;
  }
  if (result) {
    string terminal_status = FieldOrEmpty(trace->terminal, "status");
    if (terminal_status == "trace_error" || terminal_status == "aborted" ||
        terminal_status == "limit") {
      *err = "scheduler trace is incomplete (terminal status '" +
             terminal_status + "')";
      result = false;
    }
  }
  return result;
}

bool IsKnownEvent(const string& type) {
  static const char* const kKnownEvents[] = {
    "plan",           "plan_dependency", "eligible",        "delayed",
    "ready",          "selected",        "capacity",        "started",
    "outcome",        "injected",        "completed",       "dynamic_input",
    "dynamic_output", "dynamic_restat",  "deps_discovered", "restat",
    "suppressed",     "manifest_state",
  };
  for (const char* known : kKnownEvents) {
    if (type == known)
      return true;
  }
  return false;
}

string EventContext(const ParsedEvent& event) {
  string context =
      "event " + Uint64ToString(event.sequence) + " (" + event.type;
  string edge = FieldOrEmpty(event.fields, "edge");
  string label = FieldOrEmpty(event.fields, "label");
  if (!edge.empty()) {
    context += ", edge " + edge;
    if (!label.empty())
      context += " '" + label + "'";
  }
  context += ")";
  return context;
}

bool ReplayMismatch(const ParsedEvent& event, const string& message,
                    string* err) {
  *err =
      "scheduler replay divergence at " + EventContext(event) + ": " + message;
  return false;
}

enum ReplayPhase {
  kUnseen,
  kPlanned,
  kEligible,
  kDelayed,
  kReady,
  kSelected,
  kStarted,
  kCompletedSuccess,
  kCompletedFailure,
  kSuppressed,
};

struct ReplayEdgeState {
  const Edge* graph_edge = nullptr;
  ReplayPhase phase = kUnseen;
  vector<string> dependencies;
  bool pool_reserved = false;
  bool has_outcome = false;
  int outcome_status = 0;
  string outcome_source;
  bool has_injection = false;
  int injection_status = 0;
  bool can_suppress = false;
};

bool PhaseIsComplete(ReplayPhase phase) {
  return phase == kCompletedSuccess || phase == kSuppressed;
}

bool WriteParsedTrace(const string& path, const ParsedTrace& original,
                      const vector<const ParsedEvent*>& events,
                      const string& reduced_for, uint64_t max_bytes,
                      string* err) {
  FILE* file = fopen(path.c_str(), "wb");
  if (!file) {
    *err = "opening reduced scheduler trace '" + path + "': " + strerror(errno);
    return false;
  }

  uint64_t bytes = 0;
  auto write_line = [&](const string& line) -> bool {
    if (line.size() > SchedulerTrace::kMaxLineBytes) {
      *err = "reduced scheduler trace event exceeds the line limit";
      return false;
    }
    if (bytes + line.size() + 1 > max_bytes) {
      *err = "reduced scheduler trace exceeds the configured byte limit";
      return false;
    }
    if (fwrite(line.data(), 1, line.size(), file) != line.size() ||
        fputc('\n', file) == EOF) {
      *err =
          "writing reduced scheduler trace '" + path + "': " + strerror(errno);
      return false;
    }
    bytes += line.size() + 1;
    return true;
  };

  string header = kTraceMagic;
  for (const SchedulerTraceField& field : original.header) {
    if (field.first == "version" || field.first == "requires" ||
        field.first == "reduced_for") {
      continue;
    }
    AppendField(&header, field.first, field.second);
  }
  AppendField(&header, "version", "2");
  AppendField(&header, "requires", kCausalSemantics);
  AppendField(&header, "reduced_for", reduced_for);

  bool result = write_line(header);
  uint64_t sequence = 1;
  map<string, uint64_t> last_edge_lane;
  for (const ParsedEvent* event : events) {
    if (!result)
      break;
    string line = "event";
    AppendField(&line, "seq", Uint64ToString(sequence));
    AppendField(&line, "type", event->type);
    string edge;
    string cause;
    for (const SchedulerTraceField& field : event->fields) {
      if (field.first == "seq" || field.first == "type" ||
          field.first == "lane" || field.first == "previous" ||
          field.first == "cause_event") {
        continue;
      }
      AppendField(&line, field.first, field.second);
      if (field.first == "edge")
        edge = field.second;
      else if (field.first == "cause")
        cause = field.second;
    }
    if (!edge.empty()) {
      map<string, uint64_t>::const_iterator previous = last_edge_lane.find(edge);
      const uint64_t lane =
          previous == last_edge_lane.end() ? 1 : previous->second + 1;
      AppendField(&line, "lane", Uint64ToString(lane));
      if (previous != last_edge_lane.end())
        AppendField(&line, "previous", Uint64ToString(previous->second));
    }
    if (!cause.empty()) {
      map<string, uint64_t>::const_iterator cause_event =
          last_edge_lane.find(cause);
      if (cause_event != last_edge_lane.end())
        AppendField(&line, "cause_event", Uint64ToString(cause_event->second));
    }
    result = write_line(line);
    if (!edge.empty()) {
      map<string, uint64_t>::const_iterator previous = last_edge_lane.find(edge);
      last_edge_lane[edge] =
          previous == last_edge_lane.end() ? 1 : previous->second + 1;
    }
    ++sequence;
  }
  if (result) {
    string terminal = "end";
    AppendField(&terminal, "status", FieldOrEmpty(original.terminal, "status"));
    AppendField(&terminal, "events", Uint64ToString(sequence - 1));
    AppendField(&terminal, "reduced", "1");
    result = write_line(terminal);
  }
  if (result && fflush(file) != 0) {
    *err =
        "flushing reduced scheduler trace '" + path + "': " + strerror(errno);
    result = false;
  }
  if (fclose(file) != 0 && result) {
    *err = "closing reduced scheduler trace '" + path + "': " + strerror(errno);
    result = false;
  }
  return result;
}

}  // namespace

const size_t SchedulerTrace::kDefaultMaxEvents;
const uint64_t SchedulerTrace::kDefaultMaxBytes;
const size_t SchedulerTrace::kMaxLineBytes;

SchedulerTrace::SchedulerTrace(FILE* file, string path, size_t max_events,
                               uint64_t max_bytes)
    : file_(file), path_(std::move(path)), max_events_(max_events),
      max_bytes_(max_bytes), bytes_written_(0), next_sequence_(1),
      finished_(false) {}

SchedulerTrace::~SchedulerTrace() {
  if (!file_)
    return;
  if (!finished_) {
    string line = "end";
    AppendField(&line, "status", error_.empty() ? "aborted" : "trace_error");
    AppendField(&line, "events", Uint64ToString(next_sequence_ - 1));
    fwrite(line.data(), 1, line.size(), file_);
    fputc('\n', file_);
    fflush(file_);
  }
  fclose(file_);
}

unique_ptr<SchedulerTrace> SchedulerTrace::Open(
    const string& path, const string& graph_digest, int parallelism,
    int failures_allowed, int manifest_generation, size_t max_events,
    uint64_t max_bytes, string* err) {
  FILE* file = fopen(path.c_str(), "wb");
  if (!file) {
    *err = "opening scheduler trace '" + path + "': " + strerror(errno);
    return nullptr;
  }
  ++g_operation_counts.files_opened;
  unique_ptr<SchedulerTrace> trace(
      new SchedulerTrace(file, path, max_events, max_bytes));
  if (!trace->WriteHeader(graph_digest, parallelism, failures_allowed,
                          manifest_generation)) {
    *err = trace->error();
    return nullptr;
  }
  return trace;
}

bool SchedulerTrace::WriteHeader(const string& graph_digest, int parallelism,
                                 int failures_allowed,
                                 int manifest_generation) {
  string line = kTraceMagic;
  AppendField(&line, "version", "2");
  AppendField(&line, "requires", kCausalSemantics);
  AppendField(&line, "graph", graph_digest);
  AppendField(&line, "parallelism", IntToString(parallelism));
  AppendField(&line, "failures_allowed", IntToString(failures_allowed));
  AppendField(&line, "manifest_generation", IntToString(manifest_generation));
  return WriteLine(line);
}

bool SchedulerTrace::RecordEdgeEvent(const string& type, const Edge* edge,
                                     const SchedulerTraceFields& fields,
                                     const Edge* cause) {
  if (!edge) {
    SetError("attempted to trace an event without an edge");
    return false;
  }
  const string edge_id = EdgeId(edge);
  SchedulerTraceFields all_fields;
  all_fields.reserve(fields.size() + 6);
  all_fields.push_back(make_pair("edge", edge_id));
  all_fields.push_back(make_pair("label", EdgeLabel(edge)));
  all_fields.insert(all_fields.end(), fields.begin(), fields.end());

  map<string, uint64_t>::const_iterator previous =
      last_edge_lane_.find(edge_id);
  const uint64_t lane =
      previous == last_edge_lane_.end() ? 1 : previous->second + 1;
  all_fields.push_back(make_pair("lane", Uint64ToString(lane)));
  if (previous != last_edge_lane_.end()) {
    all_fields.push_back(
        make_pair("previous", Uint64ToString(previous->second)));
  }
  if (cause) {
    const string cause_id = EdgeId(cause);
    map<string, uint64_t>::const_iterator cause_event =
        last_edge_lane_.find(cause_id);
    if (cause_event != last_edge_lane_.end()) {
      all_fields.push_back(make_pair("cause", cause_id));
      all_fields.push_back(
          make_pair("cause_event", Uint64ToString(cause_event->second)));
    }
  }
  if (!WriteEvent(type, all_fields, edge_id))
    return false;
  last_edge_lane_[edge_id] = lane;
  return true;
}

bool SchedulerTrace::RecordEvent(const string& type,
                                 const SchedulerTraceFields& fields) {
  return WriteEvent(type, fields, string());
}

bool SchedulerTrace::WriteEvent(const string& type,
                                const SchedulerTraceFields& fields,
                                const string&) {
  if (!ok())
    return false;
  if (next_sequence_ > max_events_) {
    SetError("scheduler trace exceeds the configured event limit of " +
             Uint64ToString(max_events_));
    return false;
  }
  if (type.empty()) {
    SetError("attempted to write a scheduler trace event with no type");
    return false;
  }
  string line = "event";
  AppendField(&line, "seq", Uint64ToString(next_sequence_));
  AppendField(&line, "type", type);
  set<string> field_names;
  field_names.insert("seq");
  field_names.insert("type");
  for (const SchedulerTraceField& field : fields) {
    if (!IsFieldName(field.first)) {
      SetError("invalid scheduler trace field name '" + field.first + "'");
      return false;
    }
    if (!field_names.insert(field.first).second) {
      SetError("duplicate scheduler trace field '" + field.first + "'");
      return false;
    }
    AppendField(&line, field.first, field.second);
  }
  if (!WriteLine(line))
    return false;
  ++next_sequence_;
  ++g_operation_counts.events_serialized;
  return true;
}

bool SchedulerTrace::WriteLine(const string& line) {
  if (!file_ || !ok())
    return false;
  if (line.size() > kMaxLineBytes) {
    SetError("scheduler trace event exceeds the line limit of " +
             Uint64ToString(kMaxLineBytes) + " bytes");
    return false;
  }
  if (bytes_written_ + line.size() + 1 > max_bytes_) {
    SetError("scheduler trace exceeds the configured byte limit of " +
             Uint64ToString(max_bytes_));
    return false;
  }
  if (fwrite(line.data(), 1, line.size(), file_) != line.size() ||
      fputc('\n', file_) == EOF || fflush(file_) != 0) {
    SetError("writing scheduler trace '" + path_ + "': " + strerror(errno));
    return false;
  }
  bytes_written_ += line.size() + 1;
  g_operation_counts.bytes_written += line.size() + 1;
  return true;
}

void SchedulerTrace::SetError(const string& message) {
  if (error_.empty())
    error_ = message;
}

bool SchedulerTrace::Finish(const string& status, string* err) {
  if (finished_) {
    if (!ok() && err)
      *err = error_;
    return ok();
  }
  if (!ok()) {
    string line = "end";
    AppendField(&line, "status", "trace_error");
    AppendField(&line, "events", Uint64ToString(next_sequence_ - 1));
    if (file_ && bytes_written_ + line.size() + 1 <= max_bytes_) {
      fwrite(line.data(), 1, line.size(), file_);
      fputc('\n', file_);
      fflush(file_);
    }
  } else {
    string line = "end";
    AppendField(&line, "status", status);
    AppendField(&line, "events", Uint64ToString(next_sequence_ - 1));
    WriteLine(line);
  }
  finished_ = true;
  if (file_) {
    if (fclose(file_) != 0 && ok())
      SetError("closing scheduler trace '" + path_ + "': " + strerror(errno));
    file_ = nullptr;
  }
  if (!ok() && err)
    *err = error_;
  return ok();
}

string SchedulerTrace::EdgeId(const Edge* edge) {
  StableHasher hash;
  hash.Add("ninja-edge-v1");
  // The first declared output is unique in a valid Ninja graph and is never
  // displaced by dyndep-discovered implicit outputs.  This keeps identity
  // stable before and after a dynamic graph update.
  if (edge && !edge->outputs_.empty())
    hash.Add(edge->outputs_[0]->path());
  else
    hash.Add("<no-output>");
  return "e" + hash.Hex64();
}

string SchedulerTrace::EdgeLabel(const Edge* edge) {
  if (!edge || edge->outputs_.empty())
    return "<no-output>";
  return edge->outputs_[0]->path();
}

string SchedulerTrace::GraphDigest(const State& state) {
  vector<string> definitions;
  definitions.reserve(state.edges_.size() + state.pools_.size());
  for (const Edge* edge : state.edges_) {
    string definition;
    auto add = [&definition](const string& key, const string& value) {
      definition.append(key);
      definition.push_back('\0');
      definition.append(value);
      definition.push_back('\0');
    };
    add("edge", EdgeId(edge));
    add("rule", edge->rule().name());
    add("pool", PoolName(edge));
    add("pool_depth", IntToString(edge->pool() ? edge->pool()->depth() : 0));
    add("implicit_outs", IntToString(edge->implicit_outs_));
    add("implicit_deps", IntToString(edge->implicit_deps_));
    add("order_only_deps", IntToString(edge->order_only_deps_));
    for (const Node* output : edge->outputs_)
      add("out", output->path());
    for (const Node* input : edge->inputs_)
      add("in", input->path());
    for (const Node* validation : edge->validations_)
      add("validation", validation->path());
    if (edge->dyndep_)
      add("dyndep", edge->dyndep_->path());
    add("command", edge->EvaluateCommand(true));
    add("deps", edge->GetBinding("deps"));
    add("depfile", edge->GetUnescapedDepfile());
    add("restat", edge->GetBinding("restat"));
    add("generator", edge->GetBinding("generator"));
    definitions.push_back(std::move(definition));
  }
  for (const pair<const string, Pool*>& pool : state.pools_) {
    string definition = "pool";
    definition.push_back('\0');
    definition.append(pool.first.empty() ? "default" : pool.first);
    definition.push_back('\0');
    definition.append(IntToString(pool.second->depth()));
    definitions.push_back(std::move(definition));
  }
  sort(definitions.begin(), definitions.end());
  StableHasher hash;
  hash.Add("ninja-scheduler-graph-v1");
  for (const string& definition : definitions)
    hash.Add(definition);
  return hash.Hex128();
}

bool SchedulerTrace::Replay(const string& path, const State& state,
                            const string& expected_graph_digest,
                            size_t max_events, uint64_t max_bytes,
                            string* err) {
  ParsedTrace trace;
  if (!ReadTrace(path, max_events, max_bytes, &trace, err))
    return false;

  const string trace_graph = FieldOrEmpty(trace.header, "graph");
  const string current_graph = expected_graph_digest.empty()
                                   ? GraphDigest(state)
                                   : expected_graph_digest;
  if (trace_graph.empty() || trace_graph != current_graph) {
    *err = "scheduler trace graph " +
           (trace_graph.empty() ? string("<missing>") : trace_graph) +
           " is incompatible with current build graph " + current_graph;
    return false;
  }

  unordered_map<string, const Edge*> graph_edges;
  for (const Edge* edge : state.edges_) {
    string id = EdgeId(edge);
    pair<unordered_map<string, const Edge*>::iterator, bool> inserted =
        graph_edges.insert(make_pair(id, edge));
    if (!inserted.second && inserted.first->second != edge) {
      *err =
          "current build graph has a stable edge identity collision at " + id;
      return false;
    }
  }

  unordered_map<string, ReplayEdgeState> edges;
  for (const pair<const string, const Edge*>& graph_edge : graph_edges)
    edges[graph_edge.first].graph_edge = graph_edge.second;

  unordered_map<string, uint64_t> last_edge_lane;
  unordered_map<string, int> pool_use;
  unordered_map<string, uint64_t> first_dynamic_update;
  set<string> planned_edges;
  for (const ParsedEvent& event : trace.events) {
    string edge = FieldOrEmpty(event.fields, "edge");
    if (event.type == "plan" && !edge.empty())
      planned_edges.insert(edge);
    if (event.type != "dynamic_input" && event.type != "dynamic_output" &&
        event.type != "dynamic_restat") {
      continue;
    }
    if (!edge.empty() && !first_dynamic_update.count(edge))
      first_dynamic_update[edge] = event.sequence;
  }

  int active_commands = 0;
  bool command_capacity_known = false;
  uint64_t command_capacity = 0;
  int recorded_parallelism = 0;
  const string parallelism = FieldOrEmpty(trace.header, "parallelism");
  if (!parallelism.empty() &&
      (!ParseSigned(parallelism, &recorded_parallelism) ||
       recorded_parallelism <= 0)) {
    *err = "scheduler trace header has an invalid parallelism";
    return false;
  }

  for (const ParsedEvent& event : trace.events) {
    const string required = FieldOrEmpty(event.fields, "requires");
    if (!required.empty()) {
      return ReplayMismatch(
          event, "unknown required event semantics '" + required + "'", err);
    }
    const bool known_event = IsKnownEvent(event.type);
    if (!known_event && FieldOrEmpty(event.fields, "optional") != "1")
      return ReplayMismatch(event, "unknown required event type", err);

    const string edge_id = FieldOrEmpty(event.fields, "edge");
    uint64_t edge_lane = event.sequence;
    ReplayEdgeState* edge_state = nullptr;
    if (!edge_id.empty()) {
      unordered_map<string, ReplayEdgeState>::iterator found =
          edges.find(edge_id);
      if (found == edges.end() || !found->second.graph_edge) {
        return ReplayMismatch(
            event, "edge is not present in the current build graph", err);
      }
      edge_state = &found->second;

      if (trace.version == 2) {
        const string* lane = FindField(event.fields, "lane");
        if (!lane || !ParseUnsigned(*lane, &edge_lane) || edge_lane == 0)
          return ReplayMismatch(event, "edge event has an invalid lane", err);
        const string* previous = FindField(event.fields, "previous");
        unordered_map<string, uint64_t>::const_iterator actual_previous =
            last_edge_lane.find(edge_id);
        const uint64_t expected_lane =
            actual_previous == last_edge_lane.end()
                ? 1
                : actual_previous->second + 1;
        if (edge_lane != expected_lane)
          return ReplayMismatch(event,
                                "edge lane is not contiguous for this edge",
                                err);
        if (actual_previous == last_edge_lane.end()) {
          if (previous) {
            return ReplayMismatch(event,
                                  "causal predecessor refers to an event that "
                                  "is not in this edge lane",
                                  err);
          }
        } else if (!previous ||
                   *previous != Uint64ToString(actual_previous->second)) {
          return ReplayMismatch(event,
                                "causal predecessor does not match the prior "
                                "event for this edge",
                                err);
        }
        const string* cause = FindField(event.fields, "cause");
        const string* cause_event = FindField(event.fields, "cause_event");
        if (cause) {
          if (cause->empty() || !cause_event || cause_event->empty())
            return ReplayMismatch(event, "causal source is incomplete", err);
          unordered_map<string, uint64_t>::const_iterator actual_cause =
              last_edge_lane.find(*cause);
          if (actual_cause == last_edge_lane.end() ||
              *cause_event != Uint64ToString(actual_cause->second)) {
            return ReplayMismatch(event,
                                  "causal source was not visible at this "
                                  "logical point",
                                  err);
          }
        } else if (cause_event) {
          return ReplayMismatch(event, "cause_event has no cause edge", err);
        }
      }
    } else if (trace.version == 2 && (FindField(event.fields, "lane") ||
                                      FindField(event.fields, "previous") ||
                                      FindField(event.fields, "cause") ||
                                      FindField(event.fields, "cause_event"))) {
      return ReplayMismatch(event,
                            "causal edge fields appear on a global event", err);
    }

    if (!known_event) {
      if (!edge_id.empty())
        last_edge_lane[edge_id] = edge_lane;
      continue;
    }

    if (event.type == "manifest_state") {
      // Informational.  The graph digest above carries compatibility.
    } else if (event.type == "plan") {
      if (!edge_state)
        return ReplayMismatch(event, "plan event has no edge", err);
      if (edge_state->phase != kUnseen)
        return ReplayMismatch(event, "edge was planned more than once", err);
      const Edge* graph_edge = edge_state->graph_edge;
      if (FieldOrEmpty(event.fields, "label") != EdgeLabel(graph_edge) ||
          FieldOrEmpty(event.fields, "rule") != graph_edge->rule().name() ||
          FieldOrEmpty(event.fields, "pool") != PoolName(graph_edge)) {
        return ReplayMismatch(event,
                              "recorded edge definition differs from the "
                              "current graph",
                              err);
      }
      int depth;
      if (!ParseSigned(FieldOrEmpty(event.fields, "pool_depth"), &depth) ||
          !graph_edge->pool() || depth != graph_edge->pool()->depth()) {
        return ReplayMismatch(event,
                              "recorded pool depth differs from the current "
                              "graph",
                              err);
      }
      for (const Node* input : graph_edge->inputs_) {
        const Edge* dependency = input->in_edge();
        if (!dependency)
          continue;
        const string dependency_id = EdgeId(dependency);
        if (planned_edges.count(dependency_id) &&
            find(edge_state->dependencies.begin(),
                 edge_state->dependencies.end(),
                 dependency_id) == edge_state->dependencies.end()) {
          edge_state->dependencies.push_back(dependency_id);
        }
      }
      edge_state->phase = kPlanned;
    } else if (event.type == "plan_dependency") {
      if (!edge_state || edge_state->phase != kPlanned)
        return ReplayMismatch(event, "dependency added before edge plan", err);
      string dependency = FieldOrEmpty(event.fields, "dependency");
      if (!graph_edges.count(dependency)) {
        return ReplayMismatch(
            event, "planned dependency is absent from current graph", err);
      }
      if (find(edge_state->dependencies.begin(), edge_state->dependencies.end(),
               dependency) == edge_state->dependencies.end())
        return ReplayMismatch(
            event, "recorded dependency is not an input of this edge", err);
    } else if (event.type == "dynamic_input") {
      if (!edge_state ||
          (edge_state->phase != kUnseen && edge_state->phase != kPlanned))
        return ReplayMismatch(
            event, "dynamic input became visible after scheduling", err);
      const string dependency = FieldOrEmpty(event.fields, "dependency");
      if (trace.version == 2 && FieldOrEmpty(event.fields, "path").empty()) {
        return ReplayMismatch(event, "dynamic input has no path", err);
      }
      if (!dependency.empty()) {
        if (!graph_edges.count(dependency)) {
          return ReplayMismatch(event,
                                "dynamic input producer is absent from the "
                                "current graph",
                                err);
        }
        if (find(edge_state->dependencies.begin(),
                 edge_state->dependencies.end(),
                 dependency) == edge_state->dependencies.end()) {
          edge_state->dependencies.push_back(dependency);
        }
      }
      const string cause = FieldOrEmpty(event.fields, "cause");
      if (!cause.empty() && edges[cause].phase != kCompletedSuccess) {
        return ReplayMismatch(
            event, "dynamic dependency source has not completed", err);
      }
    } else if (event.type == "dynamic_output" ||
               event.type == "dynamic_restat") {
      if (!edge_state ||
          (edge_state->phase != kUnseen && edge_state->phase != kPlanned))
        return ReplayMismatch(event,
                              "dynamic graph change became visible after "
                              "scheduling",
                              err);
      if (trace.version == 2) {
        if (event.type == "dynamic_output" &&
            FieldOrEmpty(event.fields, "path").empty()) {
          return ReplayMismatch(event, "dynamic output has no path", err);
        }
        if (event.type == "dynamic_restat" &&
            FieldOrEmpty(event.fields, "enabled") != "1") {
          return ReplayMismatch(event, "dynamic restat event is not enabled",
                                err);
        }
      }
      const string cause = FieldOrEmpty(event.fields, "cause");
      if (!cause.empty() && edges[cause].phase != kCompletedSuccess) {
        return ReplayMismatch(event, "dynamic graph source has not completed",
                              err);
      }
    } else if (event.type == "eligible") {
      if (!edge_state)
        return ReplayMismatch(event, "eligibility event has no edge", err);
      if (trace.version == 1 && edge_state->phase == kUnseen)
        edge_state->phase = kPlanned;
      if (edge_state->phase != kPlanned)
        return ReplayMismatch(event, "edge was not waiting to become ready",
                              err);
      unordered_map<string, uint64_t>::const_iterator future_update =
          first_dynamic_update.find(edge_id);
      if (future_update != first_dynamic_update.end() &&
          future_update->second > event.sequence) {
        return ReplayMismatch(event,
                              "edge became eligible before its dynamic graph "
                              "information was discovered",
                              err);
      }
      for (const string& dependency : edge_state->dependencies) {
        if (!PhaseIsComplete(edges[dependency].phase)) {
          return ReplayMismatch(
              event,
              "prerequisite " + dependency + " had not completed successfully",
              err);
        }
      }
      edge_state->phase = kEligible;
    } else if (event.type == "delayed") {
      if (!edge_state || edge_state->phase != kEligible)
        return ReplayMismatch(event, "edge was not eligible for a delay", err);
      const Edge* graph_edge = edge_state->graph_edge;
      const string pool = PoolName(graph_edge);
      const int depth = graph_edge->pool()->depth();
      if (trace.version == 2 &&
          FieldOrEmpty(event.fields, "reason") != "pool_capacity") {
        return ReplayMismatch(event, "delay has unknown reason", err);
      }
      if (depth == 0 || pool_use[pool] + graph_edge->weight() <= depth) {
        return ReplayMismatch(
            event, "recorded pool delay was not required by " + pool, err);
      }
      edge_state->phase = kDelayed;
    } else if (event.type == "ready") {
      if (!edge_state ||
          (edge_state->phase != kEligible && edge_state->phase != kDelayed)) {
        return ReplayMismatch(event,
                              "edge was not eligible for the ready queue", err);
      }
      const Edge* graph_edge = edge_state->graph_edge;
      const string pool = PoolName(graph_edge);
      const int depth = graph_edge->pool()->depth();
      if (depth != 0 && pool_use[pool] + graph_edge->weight() > depth) {
        return ReplayMismatch(
            event, "pool " + pool + " had no capacity for edge", err);
      }
      if (depth != 0)
        pool_use[pool] += graph_edge->weight();
      edge_state->pool_reserved = true;
      edge_state->phase = kReady;
    } else if (event.type == "selected") {
      if (!edge_state)
        return ReplayMismatch(event, "selection event has no edge", err);
      if (trace.version == 1 &&
          (edge_state->phase == kEligible || edge_state->phase == kDelayed)) {
        edge_state->phase = kReady;
      }
      if (edge_state->phase != kReady)
        return ReplayMismatch(event, "edge was selected while not runnable",
                              err);
      edge_state->phase = kSelected;
    } else if (event.type == "capacity") {
      const string reason = FieldOrEmpty(event.fields, "reason");
      if (reason == "command_capacity") {
        if (!ParseUnsigned(FieldOrEmpty(event.fields, "available"),
                           &command_capacity)) {
          return ReplayMismatch(event, "capacity has invalid availability",
                                err);
        }
        command_capacity_known = true;
      } else if (reason == "jobserver") {
        uint64_t available;
        if (!edge_state || edge_state->phase != kReady ||
            !ParseUnsigned(FieldOrEmpty(event.fields, "available"),
                           &available) ||
            available != 0) {
          return ReplayMismatch(
              event, "jobserver delay is not valid for this edge", err);
        }
      } else if (trace.version == 2) {
        return ReplayMismatch(event, "capacity has unknown reason", err);
      }
    } else if (event.type == "started") {
      if (!edge_state || edge_state->phase != kSelected)
        return ReplayMismatch(event, "edge started without being selected",
                              err);
      if (!edge_state->graph_edge->is_phony()) {
        if (command_capacity_known) {
          if (command_capacity == 0) {
            return ReplayMismatch(
                event, "command started with no recorded runner capacity", err);
          }
          --command_capacity;
        }
        if (recorded_parallelism > 0 &&
            active_commands >= recorded_parallelism &&
            FieldOrEmpty(event.fields, "jobserver") != "1") {
          return ReplayMismatch(
              event, "recorded command exceeded scheduler capacity", err);
        }
        ++active_commands;
      }
      edge_state->phase = kStarted;
    } else if (event.type == "injected") {
      if (!edge_state || edge_state->phase != kStarted)
        return ReplayMismatch(event,
                              "failure was injected before command start", err);
      int status;
      if (!ParseSigned(FieldOrEmpty(event.fields, "status"), &status) ||
          status <= 0 || status > 255) {
        return ReplayMismatch(event, "injected status is not a failure", err);
      }
      if (edge_state->graph_edge->is_phony())
        return ReplayMismatch(event, "failure was injected into a phony edge",
                              err);
      if (edge_state->has_injection)
        return ReplayMismatch(event, "failure was injected more than once",
                              err);
      edge_state->has_injection = true;
      edge_state->injection_status = status;
    } else if (event.type == "outcome") {
      if (!edge_state || edge_state->phase != kStarted)
        return ReplayMismatch(event, "outcome precedes command start", err);
      int status;
      if (!ParseSigned(FieldOrEmpty(event.fields, "status"), &status))
        return ReplayMismatch(event, "outcome has invalid status", err);
      if (edge_state->has_outcome)
        return ReplayMismatch(event, "command has more than one outcome", err);
      string source = FieldOrEmpty(event.fields, "source");
      if (source != "runner" && source != "injected")
        return ReplayMismatch(event, "outcome has unknown source", err);
      if (source == "injected" && (!edge_state->has_injection ||
                                   edge_state->injection_status != status)) {
        return ReplayMismatch(event, "outcome does not match injected failure",
                              err);
      }
      if (source == "runner" && edge_state->has_injection)
        return ReplayMismatch(event, "injected command used runner outcome",
                              err);
      edge_state->has_outcome = true;
      edge_state->outcome_status = status;
      edge_state->outcome_source = source;
    } else if (event.type == "deps_discovered") {
      if (!edge_state || edge_state->phase != kStarted)
        return ReplayMismatch(
            event, "command metadata appeared before command start", err);
      if (trace.version == 2 &&
          (FieldOrEmpty(event.fields, "path").empty() ||
           FieldOrEmpty(event.fields, "scope") != "next_build")) {
        return ReplayMismatch(event, "discovered dependency has invalid scope",
                              err);
      }
    } else if (event.type == "restat") {
      if (!edge_state || edge_state->phase != kStarted)
        return ReplayMismatch(
            event, "command metadata appeared before command start", err);
      const string mode = FieldOrEmpty(event.fields, "mode");
      const string changed = FieldOrEmpty(event.fields, "changed");
      if (trace.version == 2 && (FieldOrEmpty(event.fields, "output").empty() ||
                                 (mode != "restat" && mode != "generator") ||
                                 (changed != "0" && changed != "1"))) {
        return ReplayMismatch(event, "restat event is malformed", err);
      }
      if (mode == "restat" && changed == "0")
        edge_state->can_suppress = true;
    } else if (event.type == "suppressed") {
      if (!edge_state || edge_state->phase != kPlanned)
        return ReplayMismatch(
            event, "restat suppressed an edge in an illegal state", err);
      const string cause = FieldOrEmpty(event.fields, "cause");
      if (cause.empty() || edges[cause].phase != kStarted ||
          !edges[cause].can_suppress) {
        return ReplayMismatch(event, "restat suppression has no active cause",
                              err);
      }
      edge_state->phase = kSuppressed;
    } else if (event.type == "completed") {
      if (!edge_state || edge_state->phase != kStarted)
        return ReplayMismatch(event, "edge completed without an active command",
                              err);
      const string result = FieldOrEmpty(event.fields, "result");
      if (result != "success" && result != "failure")
        return ReplayMismatch(event, "completion has unknown result", err);
      if (!edge_state->graph_edge->is_phony()) {
        if (!edge_state->has_outcome)
          return ReplayMismatch(event, "command completion has no outcome",
                                err);
        if ((result == "success") != (edge_state->outcome_status == 0)) {
          return ReplayMismatch(
              event, "completion disagrees with command outcome", err);
        }
        --active_commands;
      }
      if (edge_state->pool_reserved && edge_state->graph_edge->pool() &&
          edge_state->graph_edge->pool()->depth() != 0) {
        string pool = PoolName(edge_state->graph_edge);
        pool_use[pool] -= edge_state->graph_edge->weight();
        if (pool_use[pool] < 0)
          return ReplayMismatch(event, "pool usage became negative", err);
      }
      edge_state->phase =
          result == "success" ? kCompletedSuccess : kCompletedFailure;
    }

    if (!edge_id.empty())
      last_edge_lane[edge_id] = edge_lane;
  }

  const string terminal_status = FieldOrEmpty(trace.terminal, "status");
  if (terminal_status == "success") {
    for (const pair<const string, ReplayEdgeState>& edge : edges) {
      if (edge.second.phase != kUnseen &&
          edge.second.phase != kCompletedSuccess &&
          edge.second.phase != kSuppressed) {
        *err = "scheduler replay ended successfully with edge " + edge.first +
               " '" + EdgeLabel(edge.second.graph_edge) + "' incomplete";
        return false;
      }
    }
  } else if (terminal_status != "failure" && terminal_status != "interrupted") {
    *err =
        "scheduler trace has unknown terminal status '" + terminal_status + "'";
    return false;
  }
  return true;
}

bool SchedulerTrace::Reduce(const string& input_path,
                            const string& failed_edge_id,
                            const string& output_path, size_t max_events,
                            uint64_t max_bytes, string* err) {
  ParsedTrace trace;
  if (!ReadTrace(input_path, max_events, max_bytes, &trace, err))
    return false;
  const string terminal_status = FieldOrEmpty(trace.terminal, "status");
  if (terminal_status != "success" && terminal_status != "failure" &&
      terminal_status != "interrupted") {
    *err =
        "scheduler trace has unknown terminal status '" + terminal_status + "'";
    return false;
  }

  bool target_seen = false;
  bool target_failed = false;
  unordered_map<string, set<string>> prerequisites;
  unordered_map<string, string> pools;
  unordered_map<string, int> pool_depths;
  unordered_map<string, uint64_t> eligible_at;
  unordered_map<string, uint64_t> selected_at;
  unordered_map<string, uint64_t> completed_at;
  unordered_map<string, vector<string>> causes;

  for (const ParsedEvent& event : trace.events) {
    if (!FieldOrEmpty(event.fields, "requires").empty()) {
      *err = "cannot reduce " + EventContext(event) +
             " with unknown required semantics";
      return false;
    }
    if (!IsKnownEvent(event.type) &&
        FieldOrEmpty(event.fields, "optional") != "1") {
      *err = "cannot reduce " + EventContext(event) +
             " with an unknown required event type";
      return false;
    }
    string edge = FieldOrEmpty(event.fields, "edge");
    if (edge.empty())
      continue;
    if (edge == failed_edge_id)
      target_seen = true;
    string cause = FieldOrEmpty(event.fields, "cause");
    if (!cause.empty())
      causes[edge].push_back(cause);
    if (event.type == "plan") {
      pools[edge] = FieldOrEmpty(event.fields, "pool");
      int depth;
      if (!ParseSigned(FieldOrEmpty(event.fields, "pool_depth"), &depth) ||
          depth < 0) {
        *err = "cannot reduce " + EventContext(event) +
               " with an invalid pool depth";
        return false;
      }
      pool_depths[edge] = depth;
    } else if (event.type == "plan_dependency")
      prerequisites[edge].insert(FieldOrEmpty(event.fields, "dependency"));
    else if (event.type == "dynamic_input") {
      string dependency = FieldOrEmpty(event.fields, "dependency");
      if (!dependency.empty())
        prerequisites[edge].insert(dependency);
    } else if (event.type == "eligible" && !eligible_at.count(edge))
      eligible_at[edge] = event.sequence;
    else if (event.type == "selected" && !selected_at.count(edge))
      selected_at[edge] = event.sequence;
    else if (event.type == "completed") {
      completed_at[edge] = event.sequence;
      if (edge == failed_edge_id &&
          FieldOrEmpty(event.fields, "result") == "failure") {
        target_failed = true;
      }
    }
  }
  if (!target_seen) {
    *err = "failed edge identity '" + failed_edge_id +
           "' is not present in the scheduler trace";
    return false;
  }
  if (!target_failed) {
    *err = "selected edge '" + failed_edge_id +
           "' does not have a recorded failure";
    return false;
  }

  set<string> retained;
  retained.insert(failed_edge_id);
  bool changed = true;
  while (changed) {
    changed = false;
    vector<string> current(retained.begin(), retained.end());
    for (const string& edge : current) {
      for (const string& dependency : prerequisites[edge]) {
        if (!dependency.empty() && retained.insert(dependency).second)
          changed = true;
      }
      for (const string& cause : causes[edge]) {
        if (!cause.empty() && retained.insert(cause).second)
          changed = true;
      }

      // A finite pool establishes causality between otherwise independent
      // edges.  Keep contenders whose reservation overlaps this edge's wait.
      const string pool = pools[edge];
      if (pool.empty() || !pool_depths[edge] || !selected_at.count(edge))
        continue;
      uint64_t wait_start = eligible_at.count(edge) ? eligible_at[edge] : 0;
      uint64_t selection = selected_at[edge];
      for (const pair<const string, string>& candidate : pools) {
        if (candidate.first == edge || candidate.second != pool ||
            !selected_at.count(candidate.first)) {
          continue;
        }
        uint64_t candidate_selected = selected_at[candidate.first];
        uint64_t candidate_completed = completed_at.count(candidate.first)
                                           ? completed_at[candidate.first]
                                           : numeric_limits<uint64_t>::max();
        if (candidate_selected < selection &&
            candidate_completed > wait_start &&
            retained.insert(candidate.first).second) {
          changed = true;
        }
      }
    }
  }

  vector<const ParsedEvent*> events;
  for (const ParsedEvent& event : trace.events) {
    string edge = FieldOrEmpty(event.fields, "edge");
    if (!edge.empty() && retained.count(edge))
      events.push_back(&event);
    else if (event.type == "manifest_state")
      events.push_back(&event);
  }
  return WriteParsedTrace(output_path, trace, events, failed_edge_id, max_bytes,
                          err);
}

SchedulerTraceOperationCounts SchedulerTrace::GetOperationCounts() {
  return g_operation_counts;
}

void SchedulerTrace::ResetOperationCounts() {
  g_operation_counts = SchedulerTraceOperationCounts();
}
