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

#ifndef NINJA_SCHEDULER_TRACE_H_
#define NINJA_SCHEDULER_TRACE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct Edge;
struct State;

typedef std::pair<std::string, std::string> SchedulerTraceField;
typedef std::vector<SchedulerTraceField> SchedulerTraceFields;

/// Counts work performed by the optional trace subsystem.  These counters are
/// intentionally updated only inside enabled trace operations, which makes
/// them useful for guarding the disabled path in tests and benchmarks.
struct SchedulerTraceOperationCounts {
  uint64_t files_opened = 0;
  uint64_t events_serialized = 0;
  uint64_t bytes_written = 0;
};

/// Incremental recorder and offline utilities for scheduler traces.
///
/// The recorder is called only from Ninja's scheduler thread.  It therefore
/// needs no global I/O lock, and writes and flushes one bounded event at a
/// time.  Callers must not construct this class when tracing is disabled.
class SchedulerTrace {
 public:
  static const size_t kDefaultMaxEvents = 1000000;
  static const uint64_t kDefaultMaxBytes = 256 * 1024 * 1024;
  static const size_t kMaxLineBytes = 1024 * 1024;

  /// Open |path| and write a version 2 header.  Returns null on error.
  static std::unique_ptr<SchedulerTrace> Open(
      const std::string& path, const std::string& graph_digest, int parallelism,
      int failures_allowed, int manifest_generation, size_t max_events,
      uint64_t max_bytes, std::string* err);

  ~SchedulerTrace();

  /// Write a scheduler event associated with |edge|.  Version 2 automatically
  /// adds a predecessor within the edge's causal lane and, when supplied, the
  /// most recent event for |cause|.  Field names are unescaped ASCII; values
  /// are escaped by the writer.
  bool RecordEdgeEvent(const std::string& type, const Edge* edge,
                       const SchedulerTraceFields& fields,
                       const Edge* cause = nullptr);

  bool RecordEdgeEvent(const std::string& type, const Edge* edge,
                       const Edge* cause = nullptr) {
    return RecordEdgeEvent(type, edge, SchedulerTraceFields(), cause);
  }

  /// Write an event that is not owned by one edge.
  bool RecordEvent(const std::string& type, const SchedulerTraceFields& fields);

  /// Write the required terminal record.  A trace without this record is a
  /// detectable prefix, never a successful complete trace.
  bool Finish(const std::string& status, std::string* err);

  bool ok() const { return error_.empty(); }
  const std::string& error() const { return error_; }

  /// Stable identities use canonical graph paths and never process addresses
  /// or container iteration order.
  static std::string EdgeId(const Edge* edge);
  static std::string EdgeLabel(const Edge* edge);
  static std::string GraphDigest(const State& state);

  /// Validate a trace against |state| without executing any commands.
  static bool Replay(const std::string& path, const State& state,
                     const std::string& expected_graph_digest,
                     size_t max_events, uint64_t max_bytes, std::string* err);

  /// Produce a replayable causal slice for |failed_edge_id|.
  static bool Reduce(const std::string& input_path,
                     const std::string& failed_edge_id,
                     const std::string& output_path, size_t max_events,
                     uint64_t max_bytes, std::string* err);

  static SchedulerTraceOperationCounts GetOperationCounts();
  static void ResetOperationCounts();

 private:
  SchedulerTrace(FILE* file, std::string path, size_t max_events,
                 uint64_t max_bytes);

  bool WriteHeader(const std::string& graph_digest, int parallelism,
                   int failures_allowed, int manifest_generation);
  bool WriteEvent(const std::string& type, const SchedulerTraceFields& fields,
                  const std::string& edge_id);
  bool WriteLine(const std::string& line);
  void SetError(const std::string& message);

  FILE* file_;
  std::string path_;
  size_t max_events_;
  uint64_t max_bytes_;
  uint64_t bytes_written_;
  uint64_t next_sequence_;
  bool finished_;
  std::string error_;
  std::map<std::string, uint64_t> last_edge_lane_;
};

#endif  // NINJA_SCHEDULER_TRACE_H_
