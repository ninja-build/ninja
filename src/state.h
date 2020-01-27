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

#ifndef NINJA_STATE_H_
#define NINJA_STATE_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
using namespace std;

#include "ninja/build_config.h"
#include "ninja/logger.h"

#include "build_log.h"
#include "disk_interface.h"
#include "eval_env.h"
#include "graph.h"
#include "hash_map.h"
#include "util.h"

namespace ninja {
struct Edge;
struct Node;
struct Rule;

/// A pool for delayed edges.
/// Pools are scoped to a State. Edges within a State will share Pools. A Pool
/// will keep a count of the total 'weight' of the currently scheduled edges. If
/// a Plan attempts to schedule an Edge which would cause the total weight to
/// exceed the depth of the Pool, the Pool will enqueue the Edge instead of
/// allowing the Plan to schedule it. The Pool will relinquish queued Edges when
/// the total scheduled weight diminishes enough (i.e. when a scheduled edge
/// completes).
struct Pool {
  Pool(const string& name, int depth)
    : name_(name), current_use_(0), depth_(depth), delayed_() {}

  // A depth of 0 is infinite
  bool is_valid() const { return depth_ >= 0; }
  int depth() const { return depth_; }
  const string& name() const { return name_; }
  int current_use() const { return current_use_; }

  /// true if the Pool might delay this edge
  bool ShouldDelayEdge() const { return depth_ != 0; }

  /// informs this Pool that the given edge is committed to be run.
  /// Pool will count this edge as using resources from this pool.
  void EdgeScheduled(const Edge& edge);

  /// informs this Pool that the given edge is no longer runnable, and should
  /// relinquish its resources back to the pool
  void EdgeFinished(const Edge& edge);

  /// adds the given edge to this Pool to be delayed.
  void DelayEdge(Edge* edge);

  /// Pool will add zero or more edges to the ready_queue
  void RetrieveReadyEdges(EdgeSet* ready_queue);

  /// Dump the Pool and its edges (useful for debugging).
  void Dump(Logger* logger) const;

 private:
  string name_;

  /// |current_use_| is the total of the weights of the edges which are
  /// currently scheduled in the Plan (i.e. the edges in Plan::ready_).
  int current_use_;
  int depth_;

  struct WeightedEdgeCmp {
    bool operator()(const Edge* a, const Edge* b) const {
      if (!a) return b;
      if (!b) return false;
      int weight_diff = a->weight() - b->weight();
      return ((weight_diff < 0) || (weight_diff == 0 && EdgeCmp()(a, b)));
    }
  };

  typedef set<Edge*, WeightedEdgeCmp> DelayedEdges;
  DelayedEdges delayed_;
};

/// Global state (file status) for a single run.
struct State  : public BuildLogUser {
  static Pool kDefaultPool;
  static Pool kConsolePool;
  static const Rule kPhonyRule;

  State();
  State(Logger* logger);
  State(Logger* logger, bool is_explaining);

  void AddPool(Pool* pool);
  Pool* LookupPool(const string& pool_name);

  Edge* AddEdge(const Rule* rule);

  Node* GetNode(StringPiece path, uint64_t slash_bits);
  Node* LookupNode(StringPiece path) const;

  void AddIn(Edge* edge, StringPiece path, uint64_t slash_bits);
  bool AddOut(Edge* edge, StringPiece path, uint64_t slash_bits);
  bool AddDefault(StringPiece path, string* error);

  /// Keeps all nodes and edges, but restores them to the
  /// state where we haven't yet examined the disk for dirty state.
  void ClearPathsAndEdges();

  /// Dump the nodes and Pools (useful for debugging).
  void Dump(Logger* logger);
  bool IsPathDead(StringPiece s) const;

  void Explain(const char* format, ...) const;

  /// @return the root node(s) of the graph. (Root nodes have no output edges).
  /// @param error where to write the error message if somethings went wrong.
  vector<Node*> RootNodes(string* error) const;
  vector<Node*> DefaultNodes(string* error) const;

  BindingEnv bindings_;
  BuildLog* build_log_;
  vector<Node*> defaults_;

  DepsLog* deps_log_;
  /// Functions for accesssing the disk.
  RealDiskInterface* disk_interface_;

  /// All the edges of the graph.
  vector<Edge*> edges_;

  /// True if we should show explanatory log messages.
  bool is_explaining_;

  /// The logger that gets messages from this state.
  Logger* logger_;

  /// Mapping of path -> Node.
  typedef ExternalStringHashMap<Node*>::Type Paths;
  Paths paths_;

  /// All the pools used in the graph.
  map<string, Pool*> pools_;

  // Time when the last command was started.
  int64_t start_time_millis_;
};
}  // namespace ninja

#endif  // NINJA_STATE_H_
