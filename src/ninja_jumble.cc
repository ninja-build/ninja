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

// This file is all the code that used to be in one file.
// TODO: split into modules, delete this file.

#include "ninja.h"

#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

#include "build_log.h"
#include "graph.h"
#include "util.h"

const Rule State::kPhonyRule("phony");

State::State() : build_log_(NULL) {
  AddRule(&kPhonyRule);
}

const Rule* State::LookupRule(const string& rule_name) {
  map<string, const Rule*>::iterator i = rules_.find(rule_name);
  if (i == rules_.end())
    return NULL;
  return i->second;
}

void State::AddRule(const Rule* rule) {
  assert(LookupRule(rule->name_) == NULL);
  rules_[rule->name_] = rule;
}

Edge* State::AddEdge(const Rule* rule) {
  Edge* edge = new Edge();
  edge->rule_ = rule;
  edge->env_ = &bindings_;
  edges_.push_back(edge);
  return edge;
}

Node* State::LookupNode(const string& path) {
  FileStat* file = stat_cache_.GetFile(path);
  if (!file->node_)
    return NULL;
  return file->node_;
}

Node* State::GetNode(const string& path) {
  FileStat* file = stat_cache_.GetFile(path);
  if (!file->node_)
    file->node_ = new Node(file);
  return file->node_;
}

void State::AddIn(Edge* edge, const string& path) {
  Node* node = GetNode(path);
  edge->inputs_.push_back(node);
  node->out_edges_.push_back(edge);
}

void State::AddOut(Edge* edge, const string& path) {
  Node* node = GetNode(path);
  edge->outputs_.push_back(node);
  if (node->in_edge_) {
    Warning("multiple rules generate %s. "
            "build will not be correct; continuing anyway", path.c_str());
  }
  node->in_edge_ = edge;
}

bool State::AddDefault(const string& path, string* err) {
  Node* node = LookupNode(path);
  if (!node) {
    *err = "unknown target '" + path + "'";
    return false;
  } 
  defaults_.push_back(node);
  return true;
}

vector<Node*> State::RootNodes(string* err) {
  vector<Node*> root_nodes;
  // Search for nodes with no output.
  for (vector<Edge*>::iterator e = edges_.begin(); e != edges_.end(); ++e) {
    for (vector<Node*>::iterator out = (*e)->outputs_.begin();
         out != (*e)->outputs_.end(); ++out) {
      if ((*out)->out_edges_.empty())
        root_nodes.push_back(*out);
    }
  }

  if (!edges_.empty() && root_nodes.empty())
    *err = "could not determine root nodes of build graph";

  assert(edges_.empty() || !root_nodes.empty());
  return root_nodes;
}

vector<Node*> State::DefaultNodes(string* err) {
  return defaults_.empty() ? RootNodes(err) : defaults_;
}
