// Copyright 2019 Google Inc. All Rights Reserved.
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

#ifndef NINJA_MISSING_DEPS_H_
#define NINJA_MISSING_DEPS_H_

#include <map>
#include <set>
#include <string>

#include <unordered_map>

#include "dyndep_file_sorted.h"

struct DepsLog;
struct DiskInterface;
struct Edge;
struct Node;
struct Rule;
struct State;

class MissingDependencyScannerDelegate {
 public:
  virtual ~MissingDependencyScannerDelegate();
  virtual void OnMissingDep(const Node* node, const std::string& path,
                            const Rule& generator) = 0;
};

class MissingDependencyPrinter : public MissingDependencyScannerDelegate {
  void OnMissingDep(const Node* node, const std::string& path, const Rule& generator);
  void OnStats(int nodes_processed, int nodes_missing_deps,
               int missing_dep_path_count, int generated_nodes,
               int generator_rules);
};

struct MissingDyndep {
  MissingDyndep(const Edge* producing, const Edge* requesting)
      : producing_(producing), requesting_(requesting) {}

  bool operator<(const MissingDyndep& other) const {
    return std::tie(producing_, requesting_) <
           std::tie(other.producing_, other.requesting_);
  }

  const Edge* producing_;
  const Edge* requesting_;
};

struct MissingDependencyScanner {
 public:
  using MissingDyndepType = std::set<std::array<std::string, 3>>;

  MissingDependencyScanner(MissingDependencyScannerDelegate* delegate,
                           DepsLog* deps_log, State* state,
                           DiskInterface* disk_interface,
                           const std::vector<Node*>& nodes);
  void ProcessNode(const Node* node);
  void PrintStats() const;
  void PrintDDStats() const;
  bool HadMissingDeps() const { return !nodes_missing_deps_.empty(); }
  bool HadMissingDyndeps() const { return !missing_dyndep_.empty(); }
  MissingDyndepType MissingDynDepDebug() const;

  void ProcessNodeDeps(const Node* node, const Edge* in_edge,
                       Node* const* dep_nodes, int dep_nodes_count);

  bool PathExistsBetween(const Edge* from, const Edge* to);
  bool PathExistsBetweenDyndep(const Edge* from, const Edge* to);

  MissingDependencyScannerDelegate* delegate_;
  DepsLog* deps_log_;
  State* state_;
  DiskInterface* disk_interface_;
  std::set<const Node*> seen_;
  std::set<const Node*> nodes_missing_deps_;
  std::set<const Node*> generated_nodes_;
  std::set<const Rule*> generator_rules_;
  int missing_dep_path_count_;

  using InnerAdjacencyMap = std::unordered_map<const Edge*, bool>;
  using AdjacencyMap = std::unordered_map<const Edge*, InnerAdjacencyMap>;

 private:
  AdjacencyMap adjacency_map_;
  const DyndepFileSorted DyndepFile_;
  std::set<MissingDyndep> missing_dyndep_;
};

#endif  // NINJA_MISSING_DEPS_H_
