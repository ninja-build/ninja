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

#include <algorithm>

#include "dyndep.h"
#include "graph.h"

namespace ninja {
void GraphViz::AddTarget(Node* node) {
  if (visited_nodes_.find(node) != visited_nodes_.end())
    return;

  string pathstr = node->path();
  replace(pathstr.begin(), pathstr.end(), '\\', '/');
  logger_->cout() << "\"" << node << "\" [label=\"" << pathstr << "\"]" << std::endl;
  visited_nodes_.insert(node);

  Edge* edge = node->in_edge();

  if (!edge) {
    // Leaf node.
    // Draw as a rect?
    return;
  }

  if (visited_edges_.find(edge) != visited_edges_.end())
    return;
  visited_edges_.insert(edge);

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
    logger_->cout() << "\"" << edge->inputs_[0] << "\" -> \"" << edge->outputs_[0] << "\" [label=\" " << edge->rule_->name() << "\"]" << std::endl;
  } else {
    logger_->cout() << "\"" << edge << "\" [label=\"" << edge->rule_->name() << "\", shape=ellipse]" << std::endl;
    for (vector<Node*>::iterator out = edge->outputs_.begin();
         out != edge->outputs_.end(); ++out) {
      logger_->cout() << "\"" << edge << "\" -> \"" << *out << "\"" << std::endl;
    }
    for (vector<Node*>::iterator in = edge->inputs_.begin();
         in != edge->inputs_.end(); ++in) {
      const char* order_only = "";
      if (edge->is_order_only(in - edge->inputs_.begin()))
        order_only = " style=dotted";
      logger_->cout() << "\"" << (*in) << "\" -> \"" << edge << "\" [arrowhead=none" << order_only << "]" << std::endl;
    }
  }

  for (vector<Node*>::iterator in = edge->inputs_.begin();
       in != edge->inputs_.end(); ++in) {
    AddTarget(*in);
  }
}

void GraphViz::Start() {
  logger_->cout() << "digraph ninja {" << std::endl;
  logger_->cout() << "rankdir=\"LR\"" << std::endl;
  logger_->cout() << "node [fontsize=10, shape=box, height=0.25]" << std::endl;
  logger_->cout() << "edge [fontsize=10]" << std::endl;
}

void GraphViz::Finish() {
  logger_->cout() << "}" << std::endl;
}
}  // namespace ninja
