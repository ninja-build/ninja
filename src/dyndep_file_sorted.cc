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

#include "dyndep_file_sorted.h"
#include <vector>
#include "graph.h"
#include "state.h"

struct NodeStoringDyndepLoader : public DyndepLoader {
 public:
  using DyndepLoader::DyndepLoader;
  bool GetDyndeps(const Node* node, DyndepFile* ddf, std::string* err) const;
};

bool NodeStoringDyndepLoader::GetDyndeps(const Node* node, DyndepFile* ddf,
                                         std::string* err) const {
  // Record that we are loading the dyndep file for this node.
  explanations_.Record(node, "loading dyndep file '%s'", node->path().c_str());

  if (!LoadDyndepFile(node, ddf, err))
    return false;

  // Check each edge that specified this node as its dyndep binding.
  for (Edge* edge : node->out_edges()) {
    if (edge->dyndep_ != node)
      continue;

    auto it = ddf->find(edge);
    if (it == ddf->end()) {
      *err = ("'" + edge->outputs_[0]->path() +
              "' "
              "not mentioned in its dyndep file "
              "'" +
              node->path() + "'");
      return false;
    }

    it->second.used_ = true;
  }

  // Reject extra outputs in dyndep file.
  for (const auto& dyndep_output : *ddf) {
    if (!dyndep_output.second.used_) {
      Edge* const edge = dyndep_output.first;
      *err = ("dyndep file '" + node->path() +
              "' mentions output "
              "'" +
              edge->outputs_[0]->path() +
              "' whose build statement "
              "does not have a dyndep binding for the file");
      return false;
    }
  }

  return true;
}

/// Loads all dyndep information reachable from the given nodes into a separate
/// structure without modifying the build graph.
class DynDepLoader {
 public:
  /// Loads dyndep information for the given nodes.
  static std::pair<DyndepFileSorted, std::map<const Edge*, std::string>> Load(
      State* state_, DiskInterface* disk_interface_, DepsLog* deps_log);

 private:
  DynDepLoader(State* state, DiskInterface* diskInterface, DepsLog* depsLog)
      : dyndep_loader_(state, diskInterface, nullptr) {}

  /// Recursively visits the given node and accumulates all dyndep
  /// information reachable from it.
  void AddTarget(const Edge* edges_);
  void RemoveDuplicates();
  /// Returns the collected dyndep information as a sorted structure.
  std::pair<DyndepFileSorted, std::map<const Edge*, std::string>>
  getDyndepFileSorted() const;

  NodeStoringDyndepLoader dyndep_loader_;
  /// Accumulates all dyndep information discovered during traversal.
  /// Represents the complete dyndep state for the current build.
  /// contains information about missing dyndep files and the warning message as
  /// well
  DyndepFile dynDepBuild_;
  std::map<const Edge*, std::string> dynDepBuildFailed_;
};

std::pair<DyndepFileSorted, std::map<const Edge*, std::string>>
DynDepLoader::Load(State* state_, DiskInterface* disk_interface_,
                   DepsLog* deps_log) {
  std::string err;
  std::vector<Edge*>& allEdges = state_->edges_;
  DynDepLoader dynDepLoader(state_, disk_interface_, deps_log);
  for (const Edge* edge : allEdges)
    dynDepLoader.AddTarget(edge);

  dynDepLoader.RemoveDuplicates();
  return dynDepLoader.getDyndepFileSorted();
}

void DynDepLoader::AddTarget(const Edge* edge) {
  DyndepFile ddf;
  if (edge->dyndep_ && edge->dyndep_->dyndep_pending()) {
    std::string err;

    // Attempt to load the dyndep file for this edge.
    if (!dyndep_loader_.GetDyndeps(edge->dyndep_, &ddf, &err)) {
      // log dyndep load warnings
      dynDepBuildFailed_.insert({ edge, err });
    } else {
      // Merge dyndep entries into the global build map.
      for (const auto& dd : ddf) {
        auto it = dynDepBuild_.insert(dd);

        // If the entry already exists, merge its implicit inputs/outputs.
        if (!it.second) {
          const Dyndeps& dd_local(dd.second);
          Dyndeps& dd_build(it.first->second);

          // Merge implicit inputs.
          if (dd_build.implicit_inputs_.empty())
            dd_build.implicit_inputs_ = std::move(dd_local.implicit_inputs_);
          else
            dd_build.implicit_inputs_.insert(dd_build.implicit_inputs_.end(),
                                             dd_local.implicit_inputs_.begin(),
                                             dd_local.implicit_inputs_.end());

          // Merge implicit outputs.
          if (dd_build.implicit_outputs_.empty())
            dd_build.implicit_outputs_ = std::move(dd_local.implicit_outputs_);
          else
            dd_build.implicit_outputs_.insert(
                dd_build.implicit_outputs_.end(),
                dd_local.implicit_outputs_.begin(),
                dd_local.implicit_outputs_.end());
        }
      }
    }
  }
}

void DynDepLoader::RemoveDuplicates() {
  for (auto& entry : dynDepBuild_) {
    auto& inputs(entry.second.implicit_inputs_);
    std::sort(inputs.begin(), inputs.end());
    inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());

    auto& outputs(entry.second.implicit_outputs_);
    std::sort(outputs.begin(), outputs.end());
    outputs.erase(std::unique(outputs.begin(), outputs.end()), outputs.end());
  }
}

std::pair<DyndepFileSorted, std::map<const Edge*, std::string>>
DynDepLoader::getDyndepFileSorted() const {
  return { std::move(DyndepFileSorted::Create(dynDepBuild_)),
           std::move(dynDepBuildFailed_) };
}

/// Prints warning from all dyndnep file that could not be loaded
class LoaderWarning {
 public:
  static void Print(const std::pair<DyndepFileSorted,
                                    std::map<const Edge*, std::string>>& data,
                    const std::vector<Node*>& nodes);
  /// For testing
  /// Returns all warning messages
  static std::vector<std::string> Get(
      const std::pair<DyndepFileSorted, std::map<const Edge*, std::string>>&
          data,
      const std::vector<Node*>& nodes);

 private:
  struct Delegate {
    virtual void print(const std::string& warning) {
      Warning("%s", warning.c_str());
    }
  };

  struct DelegateLog : public Delegate {
    virtual void print(const std::string& warning) {
      warnings_.push_back(warning);
    }
    std::vector<std::string> warnings_;
  };

  LoaderWarning(const std::pair<DyndepFileSorted,
                                std::map<const Edge*, std::string>>& data,
                const std::vector<Node*>& nodes, Delegate& delegate);

  /// Recursively visits the given node and print all dyndep
  /// wrning reachable from it.
  void AddTarget(const Node* node);

  std::set<const Node*> visited_nodes_;
  EdgeSet visited_edges_;
  Delegate& delegate_;
  /// Accumulates all dyndep information discovered during traversal.
  /// Represents the complete dyndep state for the current build.
  const std::pair<DyndepFileSorted, std::map<const Edge*, std::string>>&
      dynDepBuild_;
};

void LoaderWarning::Print(
    const std::pair<DyndepFileSorted, std::map<const Edge*, std::string>>& data,
    const std::vector<Node*>& nodes) {
  LoaderWarning::Delegate delegate;
  LoaderWarning(data, nodes, delegate);
}

std::vector<std::string> LoaderWarning::Get(
    const std::pair<DyndepFileSorted, std::map<const Edge*, std::string>>& data,
    const std::vector<Node*>& nodes) {
  LoaderWarning::DelegateLog delegate;
  LoaderWarning(data, nodes, delegate);
  return delegate.warnings_;
}

LoaderWarning::LoaderWarning(
    const std::pair<DyndepFileSorted, std::map<const Edge*, std::string>>& data,
    const std::vector<Node*>& nodes, Delegate& delegate)
    : delegate_(delegate), dynDepBuild_(data) {
  for (const auto node : nodes) {
    AddTarget(node);
  }
}

void LoaderWarning::AddTarget(const Node* node) {
  if (!visited_nodes_.insert(node).second)
    return;

  const Edge* edge = node->in_edge();

  // check dyndep for an 'in_edge'
  if (!edge) {
    auto it = dynDepBuild_.first.findOut(node);
    if (!it)
      return;  // Nodes without an incoming edge.
    edge = it;
  }

  // Skip if this edge was already processed.
  if (!visited_edges_.insert(edge).second)
    return;

  auto it = dynDepBuild_.second.find(edge);
  if (it != dynDepBuild_.second.end()) {
    delegate_.print(it->second);
  }

  // Recurse into manifest inputs
  for (auto in : edge->inputs_) {
    AddTarget(in);
  }

  // Recurse into dyndep inputs
  auto dd_in = dynDepBuild_.first.findIn(edge);
  if (dd_in)
    for (const Node* in : *dd_in) {
      AddTarget(in);
    }
}

DyndepFileSorted Load(State* state, DiskInterface* disk_interface,
                      DepsLog* deps_log, const std::vector<Node*>& nodes) {
  auto dyndepData = DynDepLoader::Load(state, disk_interface, deps_log);

  // print warnings for missing dyndeps files, reachable from nodes
  LoaderWarning::Print(dyndepData, nodes);

  return dyndepData.first;
}

std::vector<std::string> GetWarnings(State* state, DiskInterface* disk_interface,
                      DepsLog* deps_log, const std::vector<Node*>& nodes) {
  auto dyndepData = DynDepLoader::Load(state, disk_interface, deps_log);

  // get warnings for missing dyndeps files, reachable from nodes
  return LoaderWarning::Get(dyndepData, nodes);
}

DyndepFileSorted::DyndepFileSorted(
    std::unordered_map<const Node*, const Edge*>&& nodeSortedOut,
    std::unordered_map<const Edge*, std::vector<const Node*>>&& edgeSortedOut,
    std::unordered_map<const Edge*, std::vector<const Node*>>&& edgeSortedIn)
    : nodeSortedOut_(std::move(nodeSortedOut)),
      edgeSortedOut_(std::move(edgeSortedOut)),
      edgeSortedIn_(std::move(edgeSortedIn)) {}

DyndepFileSorted DyndepFileSorted::Create(const DyndepFile& dyndepFile) {
  std::unordered_map<const Node*, const Edge*> nodeSortedOut;
  std::unordered_map<const Edge*, std::vector<const Node*>> edgeSortedOut;
  std::unordered_map<const Edge*, std::vector<const Node*>> edgeSortedIn;

  nodeSortedOut.reserve(dyndepFile.size());
  edgeSortedOut.reserve(dyndepFile.size());

  for (const auto& entry : dyndepFile) {
    const Edge* edge = entry.first;
    const Dyndeps& dyndeps = entry.second;

    for (const Node* outNode : dyndeps.implicit_outputs_) {
      auto inserted = nodeSortedOut.emplace(outNode, edge);
      assert(inserted.second);
      (void)inserted;

      auto it = edgeSortedOut.emplace(edge, std::vector<const Node*>{ outNode });
      if (!it.second) {
        it.first->second.push_back(outNode);
      }
    }

    for (const Node* inNode : dyndeps.implicit_inputs_) {
      auto it = edgeSortedIn.emplace(edge, std::vector<const Node*>{ inNode });
      if (!it.second) {
        it.first->second.push_back(inNode);
      }
    }
  }

  return DyndepFileSorted(std::move(nodeSortedOut), std::move(edgeSortedOut),
                          std::move(edgeSortedIn));
}

const std::vector<const Node*>* DyndepFileSorted::findOut(
    const Edge* edge) const {
  auto it = edgeSortedOut_.find(edge);
  if (it != edgeSortedOut_.end())
    return &it->second;
  else
    return nullptr;
}

const Edge* DyndepFileSorted::findOut(const Node* node) const {
  auto it = nodeSortedOut_.find(node);
  if (it != nodeSortedOut_.end())
    return it->second;
  else
    return nullptr;
}

const std::vector<const Node*>* DyndepFileSorted::findIn(
    const Edge* edge) const {
  auto it = edgeSortedIn_.find(edge);
  if (it != edgeSortedIn_.end())
    return &it->second;
  else
    return nullptr;
}
