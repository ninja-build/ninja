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

#include "graphviz.h"

#include <stdio.h>
#include <algorithm>
#include <cassert>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "dyndep.h"
#include "elide_middle.h"
#include "graph.h"

using namespace std;

namespace {
std::string pretty(std::string name) {
  replace(name.begin(), name.end(), '\\', '/');
  return name;
}
}  // namespace

namespace graph {
bool Option::operator==(const Option& data) const {
  return depth == data.depth && input_siblings_ == data.input_siblings_ &&
         regexExclude_ == data.regexExclude_ &&
         exportOrderOnlyLinks_ == data.exportOrderOnlyLinks_ &&
         exportExplicitLinks_ == data.exportExplicitLinks_ &&
         exportImplicitLinks_ == data.exportImplicitLinks_ &&
         exportGenDepLoader_ == data.exportGenDepLoader_ &&
         relations_ == data.relations_ && reverse_ == data.reverse_;
}

bool Option::operator!=(const Option& data) const {
  return !(*this == data);
}

bool Option::excludeNode(const Node* node) const {
  if (regexExclude_.empty())
    return false;
  return std::regex_match(pretty(node->path()), std::regex(regexExclude_));
}

bool Option::checkRegex() const {
  if (regexExclude_.empty())
    return true;

  try {
    std::regex temp(regexExclude_);
  } catch (const std::regex_error&) {
    return false;
  }
  return true;
}

bool Group::operator==(const Group& data) const {
  return options_ == data.options_ && targets_ == data.targets_;
}

bool Group::operator!=(const Group& data) const {
  return !(*this == data);
}

void Options::pushGroup(const graph::Group& myGroup, const bool myQuery) {
  if (myQuery) {
    // 2 groups to simulate query option
    // ninja -t graph -d0 -s [Targets] -d0 -s -r [Targets]
    graph::Group myGroup1 = myGroup;
    myGroup1.options_.input_siblings_ = false;
    myGroup1.options_.relations_ = false;
    if (myGroup1.options_.depth == -1)
      myGroup1.options_.depth = 0;

    graph::Group myGroup2 = myGroup1;
    myGroup2.options_.reverse_ = true;
    myGroup1.options_.reverse_ = false;

    Groups_.push_back(myGroup1);
    Groups_.push_back(myGroup2);
  } else {
    Groups_.push_back(myGroup);
  }
}
}  // namespace graph

void DepLoader::Load(State* state, DiskInterface* disk_interface,
                     DepsLog* deps_log, std::set<Node*>& nodes, bool loadDynDep,
                     bool loadDep) {
  for (int i = 0; i < 10; i++) {
    // not all dyndeps could be loaded in one iteration
    // limit the iteration to 10
    DepLoader temp(state, disk_interface, deps_log, loadDynDep, loadDep);

    for (auto node : nodes)
      temp.AddTarget(node);

    // if no dyndeps has been loaded stop iteration
    if (!temp.DynDepLoaded())
      break;
  }
}

void DepLoader::AddTarget(Node* node) {
  if (!visited_nodes_.insert(node).second)
    return;

  Edge* edge = node->in_edge();

  if (!edge)
    return;

  if (!visited_edges_.insert(edge).second)
    return;

  if (loadDynDep_ && edge->dyndep_ && edge->dyndep_->dyndep_pending()) {
    std::string err;
    if (!dyndep_loader_.LoadDyndeps(edge->dyndep_, &err)) {
      Warning("%s\n", err.c_str());
    } else
      loadedDynDep_ = true;
  }

  if (loadDep_ && !edge->deps_loaded_) {
    std::string err;
    edge->deps_loaded_ = true;
    if (!dep_loader_.LoadDeps(edge, &err)) {
      if (err.empty()) {
        edge->deps_missing_ = true;
      }
    } else
      loadedDynDep_ = true;
  }

  for (auto in : edge->inputs_) {
    AddTarget(in);
  }
}

namespace {

linktype getLinktypeInput(const Edge* edge, std::size_t index) {
  if (edge->is_order_only(index))
    return linktype::inputorderOnly;
  if (edge->is_implicit(index))
    return linktype::inputimplicit;
  return linktype::inputexplicit;
};

/// do not set cyclic = true to false
/// do set cyclic = false to true
void merge(const std::set<nodeAttribute>& input,
           std::set<nodeAttribute>& inoutput) {
  for (const auto& element : input) {
    auto iter = inoutput.insert(element);
    if (!iter.second && (!iter.first->cyclic_ && element.cyclic_)) {
      nodeAttribute updated = *iter.first;
      updated.cyclic_ = true;
      inoutput.insert(inoutput.erase(iter.first), updated);
    }
  }
}

class path;
/// Forward iterator for path
class constIteratorPath {
 public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = std::pair<const Edge*, nodeAttribute>;
  using difference_type = std::ptrdiff_t;
  using pointer = const value_type*;
  using reference = const value_type&;
  using rvalue_reference = value_type&&;

  constIteratorPath() = default;
  constIteratorPath(const path* ptr) : ptr_(ptr) {}

  constIteratorPath& operator++();
  constIteratorPath operator++(int);

  bool operator==(const constIteratorPath& data) { return data.ptr_ == ptr_; }
  bool operator!=(const constIteratorPath& data) { return data.ptr_ != ptr_; }

  reference operator*();

 private:
  const path* ptr_ = nullptr;
};

/// This is a single linked list for storing elements of type
/// std::pair<const Edge*, nodeAttribute>.
class path {
 public:
  using value_type = std::pair<const Edge*, nodeAttribute>;
  using reference = value_type&;
  using iterator = constIteratorPath;

  path() = default;
  path(const path& data);
  path(path&& data) = default;

  path& operator=(const path&) = delete;
  path& operator=(path&&) = delete;

  // function may be called only once for each object
  void emplace_front(const Edge* edge, nodeAttribute data);

  iterator begin() const;
  iterator end() const;

  friend constIteratorPath;

 private:
  value_type data_ = {};
  const path* link_ = nullptr;
};

constIteratorPath::reference constIteratorPath::operator*() {
  return ptr_->data_;
}

constIteratorPath& constIteratorPath::operator++() {
  if (ptr_ == nullptr)
    return *this;
  else {
    ptr_ = ptr_->link_;
    return *this;
  }
}

constIteratorPath constIteratorPath::operator++(int) {
  constIteratorPath tmp(*this);
  ++(*this);
  return tmp;
}

path::path(const path& data) : link_(&data) {}

void path::emplace_front(const Edge* edge, nodeAttribute data) {
  data_ = value_type(edge, data);
}

path::iterator path::begin() const {
  if (data_.first == nullptr && link_ == nullptr)
    return end();
  else if (data_.first == nullptr && link_ != nullptr)
    return link_->begin();
  else
    return constIteratorPath(this);
}
path::iterator path::end() const {
  return constIteratorPath();
}

// additionally maintain the recursion depth
struct nodeAttributeFilter : public edgeAttribute {
  nodeAttributeFilter(int depth, std::set<nodeAttribute> mySet)
      : edgeAttribute(mySet), depth_(depth) {}
  int depth_ = -1;
};

class graph_filter {
 public:
  // returns a white list of links from edge to node to be exported
  static exportLinks get(const Node* node, graph::Option options) {
    return graph_filter(node, options).get();
  }

 private:
  graph_filter(const Node* node, graph::Option options);

  exportLinks get();

  void addTarget(const Node* node, int depth, path myPath);
  void addTargetReverse(const Node* node, int depth, path& myPath);
  void setCycle(constIteratorPath beg, constIteratorPath end);
  int decrement(int depth) const;
  const graph::Option options_;
  /// contains all nodes to be exported and their respective links to be
  /// exported
  std::map<const Edge*, nodeAttributeFilter> export_links_;
  using cycleType = std::vector<path::value_type>;
};

graph_filter::graph_filter(const Node* node, graph::Option options)
    : options_(options) {
  if (options.reverse_) {
    path temp;
    addTargetReverse(node, options.depth, temp);
  } else
    addTarget(node, options.depth, path());
}

exportLinks graph_filter::get() {
  // this object is not valid after calling this function, due to move semantic
  exportLinks ret;

  for (std::pair<const Edge* const, nodeAttributeFilter>& links : export_links_) {
    auto& nodes = ret[links.first];
    nodes.cycle_ = links.second.cycle_;
    nodes.set_ = std::move(links.second.set_);
  }

  return ret;
}

void graph_filter::setCycle(constIteratorPath beg, constIteratorPath end) {
  for (auto iter = beg; iter != end; iter++) {
    auto iter2 = export_links_.find((*iter).first);
    assert(iter2 != export_links_.end());
    iter2->second.cycle_ = true;
    std::set<nodeAttribute>& temp(iter2->second.set_);
    std::set<nodeAttribute>::iterator makecyclic = temp.find((*iter).second);
    assert(makecyclic != temp.end());
    nodeAttribute copy = *makecyclic;
    copy.cyclic_ = true;
    temp.insert(temp.erase(makecyclic), copy);
  }
}

int graph_filter::decrement(int depth) const {
  if (depth <= -1)
    return -1;
  else
    return depth - 1;
}

void graph_filter::addTargetReverse(const Node* const node, const int depth,
                                    path& myPath) {
  const auto edges = node->out_edges();
  for (const auto edge : edges) {
    if (!edge) {
      // Leaf node.
      // Draw as a rect?
      continue;
    }
    const linktype ltype = [edge, node]() {
      // expensive... better way?
      const auto nodeIter =
          std::find(edge->inputs_.begin(), edge->inputs_.end(), node);
      assert(nodeIter != edge->inputs_.end());
      const std::size_t i = std::distance(edge->inputs_.begin(), nodeIter);
      return getLinktypeInput(edge, i);
    }();

    if ((ltype != linktype::inputexplicit || !options_.exportExplicitLinks_) &&
        (ltype != linktype::inputorderOnly || !options_.exportOrderOnlyLinks_) &&
        (ltype != linktype::inputimplicit || !options_.exportImplicitLinks_))
      continue;

    path nextPath = myPath;
    {
      // check for cyclic dependency, prevent eternal loop
      auto startSearch = nextPath.begin();
      nextPath.emplace_front(edge, nodeAttribute{ node, ltype });
      auto cyclestart = std::find_if(startSearch, nextPath.end(),
                                     [edge](constIteratorPath::reference data) {
                                       return data.first == edge;
                                     });
      if (cyclestart != nextPath.end()) {
        // cycle detected, mark the links as a cycle
        setCycle(nextPath.begin(), std::next(cyclestart));
        continue;
      }
    }

    auto nodeIter =
        export_links_.emplace(std::pair<const Edge* const, nodeAttributeFilter>(
            edge, nodeAttributeFilter(depth, {})));
    auto it = nodeIter.first;

    if (options_.input_siblings_) {
      std::size_t i = 0;
      for (const auto input : edge->inputs_) {
        if (!options_.excludeNode(input) &&
            (options_.exportGenDepLoader_ || !input->generated_by_dep_loader()))
          it->second.set_.insert({ input, getLinktypeInput(edge, i++) });
      }
    } else
      it->second.set_.insert({ node, ltype });  // own input

    // only follow the outputs if the edge has not already been visited or the
    // recursive depth is bigger than in the previous iteration
    const bool followOutputs = [&]() {
      if (nodeIter.second)
        return true;
      else
        return it->second.depth_ < depth;
    }();

    if (followOutputs) {
      for (const Node* output : edge->outputs_) {
        if (!options_.excludeNode(output) &&
            (options_.exportGenDepLoader_ ||
             !output->generated_by_dep_loader())) {
          it->second.set_.insert({ output, linktype::output });
          if (depth != 0) {
            auto nextPath2 = nextPath;
            nextPath2.emplace_front(edge,
                                    nodeAttribute{ output, linktype::output });
            addTargetReverse(output, decrement(depth), nextPath2);
          }
        }
      }
    }
  }
}

void graph_filter::addTarget(const Node* const node, const int depth,
                             path myPath) {
  const Edge* const edge = node->in_edge();
  if (!edge) {
    // Leaf node.
    // Draw as a rect?
    return;
  }

  {
    // check for cyclic dependency, prevent eternal loop
    auto startSearch = myPath.begin();
    myPath.emplace_front(edge, nodeAttribute{ node, linktype::output });
    auto cyclestart = std::find_if(startSearch, myPath.end(),
                                   [edge](constIteratorPath::reference data) {
                                     return data.first == edge;
                                   });
    if (cyclestart != myPath.end()) {
    // cycle detected, mark the links as a cycle
    setCycle(myPath.begin(), std::next(cyclestart));
    return;
    }
  }

  auto exportIter = export_links_.emplace(
      std::pair<const Edge* const, nodeAttributeFilter>(edge, nodeAttributeFilter(depth, {})));
  auto it = exportIter.first;

  if (options_.input_siblings_) {
    for (const auto output : edge->outputs_) {
      if (!options_.excludeNode(output) &&
          (options_.exportGenDepLoader_ || !output->generated_by_dep_loader()))
        it->second.set_.insert({ output, linktype::output });
    }
  } else
    it->second.set_.insert({ node, linktype::output });  // the source

  // only follow the inputs if the edge has not already been visited or the
  // recursive depth is bigger than in the previous iteration
  const bool followInputs = [&]() {
    if (exportIter.second)
      return true;
    else
      return it->second.depth_ < depth;
  }();

  if (followInputs) {
    std::size_t index = 0;
    for (const Node* input : edge->inputs_) {
      const linktype ltype = getLinktypeInput(edge, index++);

      if (((ltype == linktype::inputexplicit && options_.exportExplicitLinks_) ||
           (ltype == linktype::inputorderOnly && options_.exportOrderOnlyLinks_) ||
           (ltype == linktype::inputimplicit && options_.exportImplicitLinks_)) &&
          !options_.excludeNode(input) &&
          (options_.exportGenDepLoader_ || !input->generated_by_dep_loader())) {
        it->second.set_.insert({ input, ltype });
        if (depth != 0){
          // Extend the current path with this input
          auto pathNext = myPath;
          pathNext.emplace_front(edge, nodeAttribute{ input, ltype });
          addTarget(input, decrement(depth), pathNext);
        }
      }
    }
  }
}

/// Class that traverses the graph and collects all dependencies in between
/// nodes.
class graph_connection {
 public:
  // returns a white list of links from edge to node to be exported
  static exportLinks get(const Node* startNode,
                         const std::vector<const Node*>& nodestoHit,
                         const graph::Option& option) {
    return graph_connection(startNode, nodestoHit, option).get();
  }

 private:
  typedef path path_type;
  graph_connection(const Node* startNode,
                   const std::vector<const Node*>& nodesToHit,
                   const graph::Option& option);
  ~graph_connection();

  exportLinks get() const { return export_links_; }

  /// @param node   The starting node to traverse from.
  /// @param path_  The current path of edges/nodes leading to this node.
  /// @return true  If traversal from this node reaches a target in nodesToHit_.
  /// @return false Otherwise.
  bool addTarget(const Node* node, path_type myPath);

  bool validLink(linktype myLink) const;

  exportLinks export_links_;
  // Edges already checked against targets: true = hit, false = miss.
  std::map<const Edge*, bool> visited_edges_;
  using cycleType = std::vector<path::value_type>;
  std::vector<cycleType> cycles_; // graph cycles
  const std::vector<const Node*> nodesToHit_;
  const graph::Option option_;
};

graph_connection::graph_connection(const Node* startNode,
                                   const std::vector<const Node*>& nodesToHit,
                                   const graph::Option& option)
    : nodesToHit_(nodesToHit), option_(option) {
  addTarget(startNode, path_type());
}

graph_connection::~graph_connection() {
  // std::cout << "__"<<cycles_.size() << std::endl;
  assert(cycles_.empty());
}

bool graph_connection::addTarget(const Node* node, path_type mypath) {
  const Edge* const edge = node->in_edge();
  bool ret = false;

  if (edge == NULL)
    return ret;

  {
    // check for cyclic dependency, prevent eternal loop
    auto cyclestart = std::find_if(mypath.begin(), mypath.end(),
                                   [edge](constIteratorPath::reference data) {
                                     return data.first == edge;
                                   });
    if (cyclestart != mypath.end()) {
      cycles_.emplace_back(mypath.begin(), std::next(cyclestart));
      cycles_.back().emplace_back(edge,
                                  nodeAttribute{ node, linktype::output });
      return ret;
    }
  }

  // add output to path
  mypath.emplace_front(edge, nodeAttribute{ node, linktype::output });

  {
    // Abort recursion if edge has already been evaluated.
    // If previously marked as hit, add the entire path to the export result
    const auto check = visited_edges_.find(edge);
    if (check != visited_edges_.end()) {
      // if hit, add all elements of the path to the result
      if (check->second) {
        for (const auto& hit : mypath) {
          export_links_[hit.first].set_.insert(hit.second);
        }
      }
      return check->second;
    }
  }

  {
    // Traverse to the next node via its input edges
    std::size_t index = 0;
    for (const Node* input : edge->inputs_) {
      const linktype ltype = getLinktypeInput(edge, index++);
      // Skip invalid links or nodes generated by the dep-loader
      if (!validLink(ltype) || input->generated_by_dep_loader())
        continue;

      // Extend the current path with this input
      auto pathInput = mypath;
      pathInput.emplace_front(edge, nodeAttribute{ input, ltype });
      {
        // Check if the input node is one of the target nodes to hit
        auto iter = std::find(nodesToHit_.begin(), nodesToHit_.end(), input);
        if (iter != nodesToHit_.end()) {
          // Target hit: add the entire path to the export result
          for (const auto& hit : pathInput) {
            export_links_[hit.first].set_.insert(hit.second);
          }
          // mark this edge as a hit for return
          ret = true;
        }
      }

      // Recursively continue traversal from this input node
      if (addTarget(input, pathInput))
        ret = true;
    }
  }

  // mark/add the cyclic subgraphs if part of export
  if (!cycles_.empty()) {
    std::vector<std::vector<cycleType>::iterator> forErase;
    for (auto cycle = cycles_.begin(); cycle != cycles_.end(); cycle++) {
      if (!cycle->empty() && cycle->front().first == edge) {
        forErase.push_back(cycle);
        if (ret)
          continue;
        for (auto iter = cycle->begin(); iter != cycle->end(); iter++) {
          auto links = export_links_.find((*iter).first);
          if (links != export_links_.end()) {
            std::set<nodeAttribute>& temp(links->second.set_);
            links->second.cycle_ = true;
            std::set<nodeAttribute>::iterator makecyclic =
                temp.find((*iter).second);
            if (makecyclic != temp.end()) {
              nodeAttribute copy = *makecyclic;
              copy.cyclic_ = true;
              temp.insert(temp.erase(makecyclic), copy);
            } else {
              nodeAttribute nodeCycle = (*iter).second;
              nodeCycle.cyclic_ = true;
              temp.insert(nodeCycle);
            }
          } else {
            nodeAttribute cycleNode = (*iter).second;
            cycleNode.cyclic_ = true;
            export_links_.emplace((*iter).first, edgeAttribute({ cycleNode }, true));
          }
        }
      }
    }
    for (auto& i : forErase) {
      cycles_.erase(i);
    }
  }

  // mark this edge to be determined
  visited_edges_[edge] = ret;
  return ret;
}

bool graph_connection::validLink(linktype myLink) const {
  return (myLink != linktype::inputexplicit || option_.exportExplicitLinks_) &&
         (myLink != linktype::inputorderOnly || option_.exportOrderOnlyLinks_) &&
         (myLink != linktype::inputimplicit || option_.exportImplicitLinks_);
}
}  // namespace

GraphViz::GraphViz(const graph::Options& groups) : elide_(groups.middleElide_) {
  for (auto& Group : groups.Groups_) {
    if (Group.options_.relations_) {
      AddTargetRelation(Group.targets_, Group.options_);
    } else
      for (auto& Target : Group.targets_) {
        AddTarget(Target, Group.options_);
      }
  }
}

void GraphViz::printDot(const graph::Options& options) {
  GraphViz(options).printDot();
}

void GraphViz::printStatistics(const graph::Options& options) {
  GraphViz(options).printStatistics();
}

void GraphViz::AddTarget(const Node* node, const graph::Option& options) {
  // get the new links and nodes to be exported
  const exportLinks newLinks = graph_filter::get(node, options);
  // merge the new links with the existing links
  for (const auto& linksOfEgde : newLinks) {
    auto& data = data_[linksOfEgde.first];
    if (!data.cycle_)
      data.cycle_ = linksOfEgde.second.cycle_;
    merge(linksOfEgde.second.set_, data.set_);
  }
}

void GraphViz::AddTargetRelation(const std::set<const Node*>& nodes, const graph::Option& options) {
  if (nodes.size() <= 1)
    return;

  for (auto node : nodes) {
    const std::vector<const Node*> nodesToHit = [&] {
      std::vector<const Node*> toHit;
      std::copy_if(nodes.begin(), nodes.end(), std::back_inserter(toHit),
                   [&node](const Node* data) { return data != node; });
      return toHit;
    }();
    const exportLinks newLinks =
        graph_connection::get(node, nodesToHit, options);

    // merge the new links with the existing links
    for (const auto& linksOfEgde : newLinks) {
      auto& data = data_[linksOfEgde.first];
      if (!data.cycle_)
        data.cycle_ = linksOfEgde.second.cycle_;
      merge(linksOfEgde.second.set_, data.set_);
    }
  }
}

void GraphViz::printLabels() const {
  const std::map<const Node*, bool> myNodes = [&] {
    std::map<const Node*, bool> myNodes;
    for (const auto& myLinks : data_) {
      for (const auto& myNode : myLinks.second.set_) {
        auto iter = myNodes.emplace(myNode.node, myNode.cyclic_);
        if (!iter.second && !iter.first->second)
          iter.first->second = myNode.cyclic_;
      }
    }
    return myNodes;
  }();

  // print labels of all nodes
  for (const auto& element : myNodes) {
    const auto node = element.first;
    const bool cycle = element.second;
    string pathstr = pretty(node->path());
    const std::string color = node->dirty() ? ",color=red" : "";
    const string tooltip = pathstr;
    if (elide_ > 0)
      ElideMiddleInPlace(pathstr, elide_);
    const std::string colorfill = cycle ? " ,fillcolor=red, style=filled" : "";
    printf("\"%p\" [label=\"%s\"%s, tooltip=\"%s\"%s]\n", node, pathstr.c_str(),
           color.c_str(), tooltip.c_str(), colorfill.c_str());
  }

  // print labels of all edges
  for (const auto& edge : data_) {
    if (edge.first->inputs_.size() == 1 && edge.first->outputs_.size() == 1)
      continue;  // do not print, as this is not used as part of the links
                 // (draw simply.)
    std::string labelName = edge.first->rule_->name();
    const string tooltip = labelName;
    if (elide_ > 0)
      ElideMiddleInPlace(labelName, elide_);
    const std::string colorfill =
        edge.second.cycle_ ? " ,fillcolor=red, style=filled" : "";
    printf("\"%p\" [label=\"%s\", tooltip=\"%s\", shape=ellipse %s]\n",
           edge.first, labelName.c_str(), tooltip.c_str(), colorfill.c_str());
  }
}

void GraphViz::printLinks() const {
  for (const auto& edge : data_) {
    const auto& nodes = edge.second;

    if (edge.first->inputs_.size() == 1 && edge.first->outputs_.size() == 1) {
      // Can draw simply.
      // Note extra space before label text -- this is cosmetic and feels
      // like a graphviz bug.

      bool cyclic(false);
      bool orderOnly(false);
      for (const auto& node : nodes.set_) {
        if (node.cyclic_)
          cyclic = true;
        if (node.linktype_ == linktype::inputorderOnly)
          orderOnly = true;
      }

      const std::string color = cyclic ? ",color=\"red\"" : "";
      const std::string linktype = orderOnly ? " ,style=\"dotted\"" : "";

      std::string labelName = edge.first->rule_->name();
      if (elide_ > 0) ElideMiddleInPlace(labelName, elide_);
      const std::string tooltip =
          pretty(edge.first->inputs_[0]->path() + " -> " +
                 edge.first->outputs_[0]->path());

      printf("\"%p\" -> \"%p\" [label=\" %s\", tooltip=\"%s\"%s%s]\n",
             edge.first->inputs_[0], edge.first->outputs_[0], labelName.c_str(),
             tooltip.c_str(), color.c_str(), linktype.c_str());
    } else {
      for (const auto node : nodes.set_) {
        if (node.linktype_ == linktype::output) {
          std::string color = node.cyclic_ ? ",color=\"red\"" : "";
          std::string tooltip =
              edge.first->rule().name() + " -> " + pretty(node.node->path());
          printf("\"%p\" -> \"%p\" [tooltip=\"%s\"%s]\n", edge.first, node.node,
                 tooltip.c_str(), color.c_str());
        } else {
          // input
          std::string color = node.cyclic_ ? ",color=\"red\"" : "";
          const char* linktype = "";
          const std::string tooltip =
              pretty(node.node->path()) + " -> " + edge.first->rule().name();
          if (node.linktype_ == linktype::inputorderOnly)
            linktype = " style=dotted";
          else if (node.linktype_ == linktype::inputimplicit)
            linktype = " style=dashed";
          printf("\"%p\" -> \"%p\" [arrowhead=none%s, tooltip=\"%s\"%s]\n",
                 node.node, edge.first, linktype, tooltip.c_str(),
                 color.c_str());
        }
      }
    }
  }
}

exportLinks GraphViz::getLinks() const {
  return data_;
}

void GraphViz::printDot() const {
  printf("digraph ninja {\n");
  printf("rankdir=\"LR\"\n");
  printf("node [fontsize=10, shape=box, height=0.25]\n");
  printf("edge [fontsize=10]\n");

  /// first print the labels of all nodes and edges...
  printLabels();

  // print the links
  printLinks();

  printf("}\n");
}

void GraphViz::printStatistics() const {
  // count edges, nodes and dependencies
  const std::size_t countEdges = data_.size();
  std::size_t countDependancies = 0;
  std::set<const Node*> nodes;
  for (const auto& i : data_) {
    countDependancies += i.second.set_.size();
    for (auto attribute : i.second.set_) {
      nodes.insert(attribute.node);
    }
  }
  const std::size_t countNodes = nodes.size();

  printf("Export contains %zu nodes, %zu edges and %zu dependencies.\n",
         countNodes, countEdges, countDependancies);
}

/// dyndep will be loaded for parameter nodes
void ActivateDyndep(State* state, DiskInterface* diskInterface,
                    DepsLog* depsLog, std::set<Node*>& nodes, bool loadDynDep,
                    bool loadDep) {
  DepLoader::Load(state, diskInterface, depsLog, nodes,
                                      loadDynDep, loadDep);
}
