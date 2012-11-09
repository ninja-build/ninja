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
#include <deque>
#include <set>
#include <string>
#include <vector>
using namespace std;

#include "eval_env.h"
#include "hash_map.h"

struct Edge;
struct Node;
struct Rule;

/// A pool for delayed edges
struct Pool {
  explicit Pool(const string& name, int depth)
    : name_(name), depth_(depth) { }

  // A depth of 0 is infinite
  bool isValid() const { return depth_ >= 0; }
  int depth() const { return depth_; }
  const string& name() const { return name_; }

  /// true if the Pool might delay this edge
  bool ShouldDelayEdge(const Edge& edge) const { return depth_ != 0; }

  /// informs this Pool that the given edge is committed to be run.
  /// Pool will count this edge as using resources from this pool.
  void EdgeScheduled(const Edge& edge);

  /// informs this Pool that the given edge is no longer runnable, and should
  /// relinquish it's resources back to the pool
  void EdgeFinished(const Edge& edge);

  /// adds the given edge to this Pool to be delayed.
  void DelayEdge(Edge* edge);

  /// Pool will add zero or more edges to the ready_queue
  void RetrieveReadyEdges(set<Edge*>* ready_queue);

  /// Dump the Pool and it's edges (useful for debugging).
  void Dump() const;

private:
  int UnitsWaiting() { return delayed_.size(); }

  string name_;

  int current_use_;
  int depth_;

  deque<Edge*> delayed_;
};

/// Global state (file status, loaded rules) for a single run.
struct State {
  static Pool kDefaultPool;
  static const Rule kPhonyRule;

  State();

  void AddRule(const Rule* rule);
  const Rule* LookupRule(const string& rule_name);

  void AddPool(Pool* pool);
  Pool* LookupPool(const string& pool_name);

  Edge* AddEdge(const Rule* rule, Pool* pool);

  Node* GetNode(StringPiece path);
  Node* LookupNode(StringPiece path);
  Node* SpellcheckNode(const string& path);

  void AddIn(Edge* edge, StringPiece path);
  void AddOut(Edge* edge, StringPiece path);
  bool AddDefault(StringPiece path, string* error);

  /// Reset state.  Keeps all nodes and edges, but restores them to the
  /// state where we haven't yet examined the disk for dirty state.
  void Reset();

  /// Dump the nodes and Pools (useful for debugging).
  void Dump();

  /// @return the root node(s) of the graph. (Root nodes have no output edges).
  /// @param error where to write the error message if somethings went wrong.
  vector<Node*> RootNodes(string* error);
  vector<Node*> DefaultNodes(string* error);

  /// Mapping of path -> Node.
  typedef ExternalStringHashMap<Node*>::Type Paths;
  Paths paths_;

  /// All the rules used in the graph.
  map<string, const Rule*> rules_;

  /// All the pools used in the graph.
  map<string, Pool*> pools_;

  /// All the edges of the graph.
  vector<Edge*> edges_;

  BindingEnv bindings_;
  vector<Node*> defaults_;
};

#endif  // NINJA_STATE_H_
