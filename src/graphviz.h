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

#ifndef NINJA_GRAPHVIZ_H_
#define NINJA_GRAPHVIZ_H_

#include <limits>
#include <set>

#include "dyndep.h"
#include "graph.h"

struct DiskInterface;
struct Node;
struct Edge;
struct State;

enum class linktype { output, inputimplicit, inputexplicit, inputorderOnly };

namespace graph {
struct Option {
  bool operator==(const Option& data) const;
  bool operator!=(const Option& data) const;
  int depth = -1;
  bool input_siblings_ = true;
  std::string regexExclude_;
  bool exportOrderOnlyLinks_ = true;
  bool exportExplicitLinks_ = true;
  bool exportImplicitLinks_ = true;
  bool exportGenDepLoader_ = true;
  bool relations_ = false;
  bool reverse_ = false;
  bool excludeNode(const Node* node) const;
  bool checkRegex() const;
};

struct Group {
  bool operator==(const Group& data) const;
  bool operator!=(const Group& data) const;
  Option options_;
  int elide_ = -1;
  std::set<const Node*> targets_;
};

struct Options {
  void pushGroup(const graph::Group& myGroup, bool myQuery);

  std::vector<Group> Groups_;
  // global option
  int middleElide_ = -1;
  bool loadDyndep_ = true;
  bool loadDep_ = false;
  bool scan_ = false;
  bool statistic_ = false;
};

}  // namespace graph

class DepLoader {
 public:
  /// dyndep and dep will be loaded
  static void Load(State* state_, DiskInterface* disk_interface_,
                   DepsLog* deps_log, std::set<Node*>& nodes, bool loadDynDep,
                   bool loadDep);

 private:
  DepLoader(State* state, DiskInterface* diskInterface, DepsLog* depsLog,
            bool loadDynDep, bool loadDep)
      : dyndep_loader_(state, diskInterface),
        dep_loader_(state, depsLog, diskInterface, nullptr, nullptr),
        loadDynDep_(loadDynDep), loadDep_(loadDep) {}

  void AddTarget(Node* node);
  bool DynDepLoaded() const { return loadedDynDep_; }

  DyndepLoader dyndep_loader_;
  ImplicitDepLoader dep_loader_;
  std::set<Node*> visited_nodes_;
  EdgeSet visited_edges_;
  bool loadedDynDep_ = false;
  const bool loadDynDep_;
  const bool loadDep_;
};

struct nodeAttribute {
  const Node* node = {};
  linktype linktype_;
  bool cyclic_ = false;
  bool operator<(const nodeAttribute& data) const { return data.node < node; }
};

struct edgeAttribute {
  edgeAttribute() {}
  edgeAttribute(const std::set<nodeAttribute>& mySet, bool myCycle = false)
      : set_(mySet), cycle_(myCycle) {}
  std::set<nodeAttribute> set_;
  bool cycle_ = false;
};

// stores dependencies between Edges and nodes
using exportLinks = std::map<const Edge*, edgeAttribute>;


/// Runs the process of creating GraphViz .dot file output.
class GraphViz {
 public:
  static void printDot(const graph::Options& options);
  static void printStatistics(const graph::Options& options);

 protected:
  GraphViz(const graph::Options& groups);

  void printDot() const;
  void printStatistics() const;
  
  exportLinks getLinks() const;

 private:
  /// add target to export in dot files
  void AddTarget(const Node* node, const graph::Option& options);
  void AddTargetRelation(const std::set<const Node*>& nodes,
                         const graph::Option& options);
  void printLabels() const;
  void printLinks() const;
  
  exportLinks data_;
  const int elide_;
};

/// all nodes will be actualized for dyndep
void ActivateDyndep(State* state, DiskInterface* diskInterface,
                    DepsLog* depsLog, std::set<Node*>& nodes, bool loadDynDep,
                    bool loadDep);

#endif  // NINJA_GRAPHVIZ_H_
