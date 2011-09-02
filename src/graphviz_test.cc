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

#include "graph.h"
#include "util.h"
#include "test.h"

// from ninja.cc
namespace {
bool CollectTargetsFromArgs(State* state, int argc, char* argv[],
                            vector<Node*>* targets, string* err) {
  if (argc == 0) {
    *targets = state->RootNodes(err);
    if (!err->empty())
      return false;
  } else {
    for (int i = 0; i < argc; ++i) {
      string path = argv[i];
      if (!CanonicalizePath(&path, err))
        return false;
      Node* node = state->LookupNode(path);
      if (node) {
        targets->push_back(node);
      } else {
        *err = "unknown target '" + path + "'";
        return false;
      }
    }
  }
  return true;
}
} // unnamed namespace

struct GraphvizTest : public StateTestWithBuiltinRules {
};

TEST_F(GraphvizTest, EscapeSlashes) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out\\out: cat in\\in\n"));

  vector<Node*> nodes;
  string err;
  ASSERT_TRUE(CollectTargetsFromArgs(&state_, 0, NULL, &nodes, &err));

  GraphViz graph;
  testing::internal::CaptureStdout();
  graph.Start();
  for (vector<Node*>::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
    graph.AddTarget(*n);
  graph.Finish();
  stringstream ss;
  Node *node = nodes[0];
  ss << "digraph ninja {\nnode [fontsize=10, shape=box, height=0.25]\nedge [fontsize=10]\n\"";
  ss << node->in_edge_->outputs_[0];
  ss << "\" [label=\"out\\\\out\"]\n\"";
  ss << node->in_edge_->inputs_[0];
  ss << "\" -> \"";
  ss << node->in_edge_->outputs_[0];
  ss << "\" [label=\" cat\"]\n\"";
  ss << node->in_edge_->inputs_[0];
  ss << "\" [label=\"in\\\\in\"]\n}\n";
  ASSERT_STREQ(ss.str().c_str(), testing::internal::GetCapturedStdout().c_str());
}
