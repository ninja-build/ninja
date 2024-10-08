// Copyright 2024 Google Inc. All Rights Reserved.
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

#ifndef NINJA_COMMAND_COLLECTOR_H_
#define NINJA_COMMAND_COLLECTOR_H_

#include <cassert>
#include <unordered_set>
#include <vector>

#include "graph.h"

/// Collects the transitive set of edges that lead into a given set
/// of starting nodes. Used to implement the `compdb-targets` tool.
///
/// When collecting inputs, the outputs of phony edges are always ignored
/// from the result, but are followed by the dependency walk.
///
/// Usage is:
/// - Create instance.
/// - Call CollectFrom() for each root node to collect edges from.
/// - Call TakeResult() to retrieve the list of edges.
///
struct CommandCollector {
  void CollectFrom(const Node* node) {
    assert(node);

    if (!visited_nodes_.insert(node).second)
      return;

    Edge* edge = node->in_edge();
    if (!edge || !visited_edges_.insert(edge).second)
      return;

    for (Node* input_node : edge->inputs_)
      CollectFrom(input_node);

    if (!edge->is_phony())
      in_edges.push_back(edge);
  }

 private:
  std::unordered_set<const Node*> visited_nodes_;
  std::unordered_set<Edge*> visited_edges_;

  /// we use a vector to preserve order from requisites to their dependents.
  /// This may help LSP server performance in languages that support modules,
  /// but it also ensures that the output of `-t compdb-targets foo` is
  /// consistent, which is useful in regression tests.
 public:
  std::vector<Edge*> in_edges;
};

#endif  //  NINJA_COMMAND_COLLECTOR_H_
