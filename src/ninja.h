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

#ifndef NINJA_NINJA_H_
#define NINJA_NINJA_H_

#include <assert.h>

#include <algorithm>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "eval_env.h"
#include "stat_cache.h"

using namespace std;

struct Edge;
struct FileStat;
struct Node;
struct Rule;

/// Global state (file status, loaded rules) for a single run.
struct State {
  State();

  StatCache* stat_cache() { return &stat_cache_; }

  void AddRule(const Rule* rule);
  const Rule* LookupRule(const string& rule_name);
  Edge* AddEdge(const Rule* rule);
  Node* GetNode(const string& path);
  Node* LookupNode(const string& path);
  void AddIn(Edge* edge, const string& path);
  void AddOut(Edge* edge, const string& path);
  bool AddDefault(const string& path, string* error);
  /// @return the root node(s) of the graph. (Root nodes have no output edges).
  /// @param error where to write the error message if somethings went wrong.
  vector<Node*> RootNodes(string* error);
  vector<Node*> DefaultNodes(string* error);

  StatCache stat_cache_;
  /// All the rules used in the graph.
  map<string, const Rule*> rules_;
  /// All the edges of the graph.
  vector<Edge*> edges_;
  BindingEnv bindings_;
  vector<Node*> defaults_;
  struct BuildLog* build_log_;

  static const Rule kPhonyRule;
};

#endif  // NINJA_NINJA_H_
