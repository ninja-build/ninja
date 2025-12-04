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

#include "dyndep.h"
#include "graph.h"

using namespace std;

void GraphViz::printNodeLabel(const Node* const node){
   if (labeled_nodes_.insert(node).second) {
    string pathstr = node->path();
    replace(pathstr.begin(), pathstr.end(), '\\', '/');
    printf("\"%p\" [label=\"%s\"]\n", node, pathstr.c_str());
  }
}

void GraphViz::printEdgeLabel(const Edge* const edge){
  if (labeled_edges_.insert(edge).second) {
      printf("\"%p\" [label=\"%s\", shape=ellipse]\n", edge,
             edge->rule_->name().c_str());
    }
}

void GraphViz::AddTarget(const Node* const node, const int depth) {
  if (!visited_nodes_.insert(node).second)
    return;

  printNodeLabel(node);

  const Edge* const edge = node->in_edge();

  if (!edge) {
    // Leaf node.
    // Draw as a rect?
    return;
  }

  if (!visited_edges_.insert(edge).second)
    return;

  if (edge->dyndep_ && edge->dyndep_->dyndep_pending()) {
    std::string err;
    if (!dyndep_loader_.LoadDyndeps(edge->dyndep_, &err)) {
      Warning("%s\n", err.c_str());
    }
  }

  if (edge->inputs_.size() == 1 && edge->outputs_.size() == 1) {
    // Can draw simply.
    // Note extra space before label text -- this is cosmetic and feels
    // like a graphviz bug.
    printNodeLabel(edge->inputs_[0]);
    printNodeLabel(edge->outputs_[0]);
    printf("\"%p\" -> \"%p\" [label=\" %s\"]\n", edge->inputs_[0],
           edge->outputs_[0], edge->rule_->name().c_str());
  } else {
    printEdgeLabel(edge);
    for (const auto out : edge->outputs_) {
      printNodeLabel(out);
      printf("\"%p\" -> \"%p\"\n", edge, out);
    }
    {
      std::size_t index = 0;
      for (const auto in : edge->inputs_) {
        const char* order_only = "";
        if (edge->is_order_only(index++))
          order_only = " style=dotted";
        printNodeLabel(in);
        printf("\"%p\" -> \"%p\" [arrowhead=none%s]\n", in, edge, order_only);
      }
    }
  }

  if (depth > 0) {
    for (const auto in : edge->inputs_) {
      AddTarget(in, depth - 1);
    }
  }
}

void GraphViz::Start() {
  printf("digraph ninja {\n");
  printf("rankdir=\"LR\"\n");
  printf("node [fontsize=10, shape=box, height=0.25]\n");
  printf("edge [fontsize=10]\n");
}

void GraphViz::Finish() {
  printf("}\n");
}
