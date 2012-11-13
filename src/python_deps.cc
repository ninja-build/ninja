// Copyright 2012 Google Inc. All Rights Reserved.
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

#include "python_deps.h"

#include <stdio.h>

#include "graph.h"

void PythonDeps::AddTarget(Node* node) {
  Edge* edge = node->in_edge();
  if (edge) {
    if (output_nodes_.find(node) != output_nodes_.end())
      return;
    output_nodes_.insert(node);
    for (vector<Node*>::iterator in = edge->inputs_.begin();
         in != edge->inputs_.end(); ++in) {
      AddTarget(*in);
    }
  } else {
    if (input_nodes_.find(node) != input_nodes_.end())
      return;
    input_nodes_.insert(node);
  }
}

void PythonDeps::Start() {
}

void PythonDeps::Finish() {
  printf("{\n");
  printf("  'inputs': [\n");
  for (set<Node*>::iterator node = input_nodes_.begin();
       node != input_nodes_.end(); ++node) {
    printf("    '%s',\n", (*node)->path().c_str());
  }
  printf("  ],\n");
  printf("  'outputs': [\n");
  for (set<Node*>::iterator node = output_nodes_.begin();
       node != output_nodes_.end(); ++node) {
    printf("    '%s',\n", (*node)->path().c_str());
  }
  printf("  ],\n");
  printf("}\n");
}
