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

#if __cplusplus >= 201103L
#include <unordered_map>
#endif

struct DepsLog;
struct DiskInterface;
struct Edge;
struct Node;
struct Rule;
struct State;

class MissingDependencyScannerDelegate {
 public:
  virtual ~MissingDependencyScannerDelegate();
  virtual void OnMissingDep(Node* node, const std::string& path,
                            const Rule& generator) = 0;
};

class MissingDependencyPrinter : public MissingDependencyScannerDelegate {
  void OnMissingDep(Node* node, const std::string& path, const Rule& generator);
  void OnStats(int nodes_processed, int nodes_missing_deps,
               int missing_dep_path_count, int generated_nodes,
               int generator_rules);
};

struct EdgePair {
  EdgePair(Edge* from, Edge* to) : from_(from), to_(to) {}
  bool operator==(const EdgePair& other) const {
    return from_ == other.from_ && to_ == other.to_;
  }
  bool operator<(const EdgePair& other) const {
    return (from_ < other.from_) ||
           ((from_ == other.from_) && (to_ < other.to_));
  }
  Edge* from_;
  Edge* to_;
};

#if __cplusplus >= 201103L
namespace std {
template <>
struct hash<EdgePair> {
  size_t operator()(const EdgePair& k) const {
    uintptr_t uint_from = uintptr_t(k.from_);
    uintptr_t uint_to = uintptr_t(k.to_);
    return hash<uintptr_t>()(uint_from ^ (uint_to >> 3));
  }
};
}  // namespace std
#endif  // __cplusplus >= 201103L

struct MissingDependencyScanner {
 public:
  MissingDependencyScanner(MissingDependencyScannerDelegate* delegate,
                           DepsLog* deps_log, State* state,
                           DiskInterface* disk_interface);
  void ProcessNode(Node* node);
  void PrintStats();
  bool HadMissingDeps() { return !nodes_missing_deps_.empty(); }

  void ProcessNodeDeps(Node* node, Node** dep_nodes, int dep_nodes_count);

  bool PathExistsBetween(Edge* from, Edge* to);

  MissingDependencyScannerDelegate* delegate_;
  DepsLog* deps_log_;
  State* state_;
  DiskInterface* disk_interface_;
  std::set<Node*> seen_;
  std::set<Node*> nodes_missing_deps_;
  std::set<Node*> generated_nodes_;
  std::set<const Rule*> generator_rules_;
  int missing_dep_path_count_;

 private:
#if __cplusplus >= 201103L
  using EdgeAdjacencyMap = std::unordered_map<EdgePair, bool>;
#else
  typedef std::map<EdgePair, bool> EdgeAdjacencyMap;
#endif
  EdgeAdjacencyMap edge_adjacency_map_;
};

#endif  // NINJA_MISSING_DEPS_H_
