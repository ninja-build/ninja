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

#ifndef NINJA_DYNDEP_FILE_SORTED_H
#define NINJA_DYNDEP_FILE_SORTED_H

#include <vector>

#include "deps_log.h"
#include "dyndep.h"

// for legacy reason, class is actually written for const pointers...
// #define CONST_D const
#define CONST_D

/// Optimized representation of DyndepFile.
///
/// Stores all dynamic dependencies in redundant data structures (sorted by
/// node and by edge) to enable fast lookup.
class DyndepFileSorted {
 public:
  static DyndepFileSorted Create(const DyndepFile& dyndepFile);

  /// Returns all dyndep-generated output nodes produced by the given edge.
  ///
  /// nullptr: if a dependency entry does not exist for the requesting edge.
  /// otherwise: pointer to the list of produced output nodes.
  const std::vector<CONST_D Node*>* findOut(const Edge* edge) const;
  /// Returns the edge that produces the given node via dyndep.
  CONST_D Edge* findOut(const Node* node) const;

  const std::vector<CONST_D Node*>* findIn(const Edge* edge) const;

 private:
  // Each node maps to the edge that produces it.
  const std::unordered_map<const Node*, CONST_D Edge*> nodeSortedOut_;

  // Each edge maps to its output nodes.
  const std::unordered_map<const Edge*, std::vector<CONST_D Node*>>
      edgeSortedOut_;

  // Each edge maps to its input nodes.
  const std::unordered_map<const Edge*, std::vector<CONST_D Node*>> edgeSortedIn_;

 private:
  DyndepFileSorted(
      std::unordered_map<const Node*, CONST_D Edge*>&& nodeSortedOut,
      std::unordered_map<const Edge*, std::vector<CONST_D Node*>>&& edgeSortedOut,
      std::unordered_map<const Edge*, std::vector<CONST_D Node*>>&& edgeSortedIn);
};

/// Loads all dyndep files referenced in the graph.
///
/// Note that all dyndep files are loaded, regardless of reachability.
DyndepFileSorted Load(State* state, DiskInterface* disk_interface,
                      DepsLog* deps_log);

#endif  // NINJA_DYNDEP_FILE_SORTED_H
