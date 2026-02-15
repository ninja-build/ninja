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

#include "missing_deps.h"

#include <string.h>

#include <iostream>

#include "depfile_parser.h"
#include "deps_log.h"
#include "disk_interface.h"
#include "graph.h"
#include "state.h"
#include "util.h"

namespace {
class PathExistsBetween {
 public:
  using AdjacencyMap = MissingDependencyScanner::AdjacencyMap;
  using InnerAdjacencyMap = MissingDependencyScanner::InnerAdjacencyMap;
  static bool get(AdjacencyMap& adjacency_map,
                  const DyndepFileSorted* DyndepOutFile, const Edge* from,
                  const Edge* to);

 private:
  PathExistsBetween(AdjacencyMap& adjacency_map,
                    const DyndepFileSorted* DyndepOutFile, const Edge* from,
                    AdjacencyMap::iterator it);

  bool process(const Edge* to);

  AdjacencyMap& adjacency_map_;
  const DyndepFileSorted* DyndepOutFile_;
  const Edge* const from_;
  const AdjacencyMap::iterator it_;
};

PathExistsBetween::PathExistsBetween(AdjacencyMap& adjacency_map,
                                     const DyndepFileSorted* DyndepOutFile,
                                     const Edge* from,
                                     AdjacencyMap::iterator it)
    : adjacency_map_(adjacency_map), DyndepOutFile_(DyndepOutFile), from_(from),
      it_(it) {}

bool PathExistsBetween::get(AdjacencyMap& adjacency_map,
                            const DyndepFileSorted* DyndepOutFile,
                            const Edge* from, const Edge* to) {
  AdjacencyMap::iterator it = adjacency_map.find(from);

  if (it == adjacency_map.end())
    it = adjacency_map.emplace(from, InnerAdjacencyMap()).first;

  PathExistsBetween pathExistsBetween(adjacency_map, DyndepOutFile, from, it);
  return pathExistsBetween.process(to);
}

bool PathExistsBetween::process(const Edge* to) {
  InnerAdjacencyMap::const_iterator inner_it = it_->second.find(to);
  if (inner_it != it_->second.end()) {
    return inner_it->second;
  }

  // check manifest inputs of edge
  for (const Node* node : to->inputs_) {
    const Edge* e = node->in_edge();
    if (e && (e == from_ || process(e))) {
      // path found
      it_->second.emplace(to, true);
      return true;
    }
  }

  // check dyndep inputs of edge
  auto it = DyndepOutFile_->findIn(to);
  if (it) {
    for (const Node* node : *it) {
      const Edge* e = node->in_edge();
      if (e && (e == from_ || process(e))) {
        // path found
        it_->second.emplace(to, true);
        return true;
      }
    }
  }

  // path does not exist
  it_->second.emplace(to, false);
  return false;
}

/// ImplicitDepLoader variant that stores dep nodes into the given output
/// without updating graph deps like the base loader does.
struct NodeStoringImplicitDepLoader : public ImplicitDepLoader {
  NodeStoringImplicitDepLoader(
      State* state, DepsLog* deps_log, DiskInterface* disk_interface,
      DepfileParserOptions const* depfile_parser_options,
      Explanations* explanations, std::vector<Node*>* dep_nodes_output)
      : ImplicitDepLoader(state, deps_log, disk_interface,
                          depfile_parser_options, explanations),
        dep_nodes_output_(dep_nodes_output) {}

  bool LoadDeps(const Edge* edge, std::string* err);
  bool LoadDepFile(const Edge* edge, const std::string& path, std::string* err);

 private:
  bool ProcessDepfileDeps(std::vector<StringPiece>* depfile_ins,
                          std::string* err);

 private:
  std::vector<Node*>* dep_nodes_output_;
};

bool NodeStoringImplicitDepLoader::LoadDepFile(const Edge* edge,
                                               const std::string& path,
                                               std::string* err) {
  std::string content;
  std::optional<DepfileParser> depfileParser =
      LoadDepFileParser(edge, path, content, err);
  if (!depfileParser)
    return false;
  return ProcessDepfileDeps(&(*depfileParser).ins_, err);
}

bool NodeStoringImplicitDepLoader::LoadDeps(const Edge* edge,
                                            std::string* err) {
  std::string depfile = edge->GetUnescapedDepfile();
  if (!depfile.empty())
    return LoadDepFile(edge, depfile, err);

  // No deps to load.
  return true;
}

bool NodeStoringImplicitDepLoader::ProcessDepfileDeps(
    std::vector<StringPiece>* depfile_ins, std::string* err) {
  for (std::vector<StringPiece>::iterator i = depfile_ins->begin();
       i != depfile_ins->end(); ++i) {
    uint64_t slash_bits;
    CanonicalizePath(const_cast<char*>(i->str_), &i->len_, &slash_bits);
    Node* node = state_->GetNode(*i, slash_bits);
    dep_nodes_output_->push_back(node);
  }
  return true;
}

}  // namespace

MissingDependencyScannerDelegate::~MissingDependencyScannerDelegate() {}

void MissingDependencyPrinter::OnMissingDep(const Node* node, const std::string& path,
                                            const Rule& generator) {
  std::cout << "Missing dep: " << node->path() << " uses " << path
            << " (generated by " << generator.name() << ")\n";
}

MissingDependencyScanner::MissingDependencyScanner(
    MissingDependencyScannerDelegate* delegate, DepsLog* deps_log, State* state,
    DiskInterface* disk_interface, const std::vector<Node*>& nodes)
    : delegate_(delegate), deps_log_(deps_log), state_(state),
      disk_interface_(disk_interface), missing_dep_path_count_(0),
      DyndepFile_(Load(state_, disk_interface_, deps_log_, nodes)) {
  for (const Node* node : nodes)
    ProcessNode(node);
}

void MissingDependencyScanner::ProcessNode(const Node* node) {
  if (!node)
    return;
  const Edge* edge = node->in_edge();
  if (!edge)
    return;
  if (!seen_.insert(node).second)
    return;

  for (Node* in : edge->inputs_) {
    ProcessNode(in);
  }

  std::string deps_type = edge->GetBinding("deps");
  if (!deps_type.empty()) {
    DepsLog::Deps* deps = deps_log_->GetDeps(node);
    if (deps)
      ProcessNodeDeps(node, deps->nodes, deps->node_count);
  } else {
    DepfileParserOptions parser_opts;
    std::vector<Node*> depfile_deps;
    NodeStoringImplicitDepLoader dep_loader(state_, deps_log_, disk_interface_,
                                            &parser_opts, nullptr,
                                            &depfile_deps);
    std::string err;
    dep_loader.LoadDeps(edge, &err);
    if (!depfile_deps.empty())
      ProcessNodeDeps(node, &depfile_deps[0],
                      static_cast<int>(depfile_deps.size()));
  }
}

void MissingDependencyScanner::ProcessNodeDeps(const Node* node,
                                               Node* const* dep_nodes,
                                               int dep_nodes_count) {
  Edge* edge = node->in_edge();
  std::set<const Edge*> deplog_edges;
  for (int i = 0; i < dep_nodes_count; ++i) {
    const Node* deplog_node = dep_nodes[i];
    // Special exception: A dep on build.ninja can be used to mean "always
    // rebuild this target when the build is reconfigured", but build.ninja is
    // often generated by a configuration tool like cmake or gn. The rest of
    // the build "implicitly" depends on the entire build being reconfigured,
    // so a missing dep path to build.ninja is not an actual missing dependency
    // problem.
    if (deplog_node->path() == "build.ninja")
      return;
    Edge* deplog_edge = deplog_node->in_edge();
    if (deplog_edge) {
      deplog_edges.insert(deplog_edge);
    } else {
      // check if the node is produced by a dyndep output
      const auto it = DyndepFile_.findOut(deplog_node);
      if (it) {
        deplog_edges.insert(it);
      }
    }
  }
  std::vector<const Edge*> missing_deps;
  for (const Edge* de : deplog_edges) {
    if (!PathExistsBetween(de, edge)) {
      missing_deps.push_back(de);
    }
  }

  if (!missing_deps.empty()) {
    std::set<std::string> missing_deps_rule_names;
    for (const Edge* ne : missing_deps) {
      // check manifest generated outputs
      for (int i = 0; i < dep_nodes_count; ++i) {
        if (dep_nodes[i]->in_edge() == ne) {
          generated_nodes_.insert(dep_nodes[i]);
          generator_rules_.insert(&ne->rule());
          missing_deps_rule_names.insert(ne->rule().name());
          delegate_->OnMissingDep(node, dep_nodes[i]->path(), ne->rule());
        }
      }

      // check dyndep generated outputs
      auto it = DyndepFile_.findOut(ne);
      if (it) {
        const auto& outputs = *it;
        generated_nodes_.insert(outputs.begin(), outputs.end());
        generator_rules_.insert(&ne->rule());
        missing_deps_rule_names.insert(ne->rule().name());
        for (const Node* out : outputs) {
          delegate_->OnMissingDep(node, out->path(), ne->rule());
        }
      }
    }
    missing_dep_path_count_ += missing_deps_rule_names.size();
    nodes_missing_deps_.insert(node);
  }
}

void MissingDependencyScanner::PrintStats() const {
  std::cout << "Processed " << seen_.size() << " nodes.\n";
  if (HadMissingDeps()) {
    std::cout << "Error: There are " << missing_dep_path_count_
              << " missing dependency paths.\n";
    std::cout << nodes_missing_deps_.size()
              << " targets had depfile dependencies on "
              << generated_nodes_.size() << " distinct generated inputs "
              << "(from " << generator_rules_.size() << " rules) "
              << " without a non-depfile dep path to the generator.\n";
    std::cout << "There might be build flakiness if any of the targets listed "
                 "above are built alone, or not late enough, in a clean output "
                 "directory.\n";
  } else {
    std::cout << "No missing dependencies on generated files found.\n";
  }
}

bool MissingDependencyScanner::PathExistsBetween(const Edge* from,
                                                 const Edge* to) {
  return PathExistsBetween::get(adjacency_map_, &DyndepFile_, from, to);
}
