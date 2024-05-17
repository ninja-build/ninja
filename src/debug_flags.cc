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

#include <stdio.h>
#include <map>
#include <vector>
#include <string>

#include "graph.h"

bool g_explaining = false;

bool g_keep_depfile = false;

bool g_keep_rsp = false;

bool g_experimental_statcache = true;

// Reasons each Node needs rebuilding, for "-d explain".
typedef std::map<const Node*, std::vector<std::string> > Explanations;
static Explanations explanations_;

void record_explanation(const Node* node, std::string explanation) {
  explanations_[node].push_back(explanation);
}

void print_explanations(FILE *stream, const Edge* edge) {
  for (std::vector<Node*>::const_iterator o = edge->outputs_.begin();
       o != edge->outputs_.end(); ++o) {
    for (std::vector<std::string>::iterator s = explanations_[*o].begin();
         s != explanations_[*o].end(); ++s) {
      fprintf(stream, "ninja explain: %s\n", (*s).c_str());
    }
  }
}
