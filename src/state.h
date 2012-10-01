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
  string name() const { return name_; }

private:
  string name_;

  int depth_;
};

/// Global state (file status, loaded rules) for a single run.
struct State {
  static const Rule kPhonyRule;
  static const Pool kDefaultPool;

  State();

  void AddRule(const Rule* rule);
  const Rule* LookupRule(const string& rule_name);

  void AddPool(const Pool* pool);
  const Pool* LookupPool(const string& pool_name);

  Edge* AddEdge(const Rule* rule, const Pool* pool);

  Node* GetNode(StringPiece path);
  Node* LookupNode(StringPiece path);
  Node* SpellcheckNode(const string& path);

  void AddIn(Edge* edge, StringPiece path);
  void AddOut(Edge* edge, StringPiece path);
  bool AddDefault(StringPiece path, string* error);

  /// Reset state.  Keeps all nodes and edges, but restores them to the
  /// state where we haven't yet examined the disk for dirty state.
  void Reset();

  /// Dump the nodes (useful for debugging).
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
  map<string, const Pool*> pools_;

  /// All the edges of the graph.
  vector<Edge*> edges_;

  BindingEnv bindings_;
  vector<Node*> defaults_;
};

#endif  // NINJA_STATE_H_
