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

#include "state.h"

#include <assert.h>
#include <stdio.h>

#include "build_log.h"
#include "deps_log.h"
#include "disk_interface.h"
#include "edit_distance.h"
#include "graph.h"
#include "metrics.h"
#include "util.h"


namespace ninja {
void Pool::EdgeScheduled(const Edge& edge) {
  if (depth_ != 0)
    current_use_ += edge.weight();
}

void Pool::EdgeFinished(const Edge& edge) {
  if (depth_ != 0)
    current_use_ -= edge.weight();
}

void Pool::DelayEdge(Edge* edge) {
  assert(depth_ != 0);
  delayed_.insert(edge);
}

void Pool::RetrieveReadyEdges(EdgeSet* ready_queue) {
  DelayedEdges::iterator it = delayed_.begin();
  while (it != delayed_.end()) {
    Edge* edge = *it;
    if (current_use_ + edge->weight() > depth_)
      break;
    ready_queue->insert(edge);
    EdgeScheduled(*edge);
    ++it;
  }
  delayed_.erase(delayed_.begin(), it);
}

void Pool::Dump() const {
  printf("%s (%d/%d) ->\n", name_.c_str(), current_use_, depth_);
  for (DelayedEdges::const_iterator it = delayed_.begin();
       it != delayed_.end(); ++it)
  {
    printf("\t");
    (*it)->Dump();
  }
}

Pool State::kDefaultPool("", 0);
Pool State::kConsolePool("console", 1);
const Rule State::kPhonyRule("phony");

State::State() : State(new LoggerBasic(), false) {}

State::State(Logger* logger) : State(logger, false) {}

State::State(Logger* logger, bool is_explaining) :
    build_log_(new BuildLog()),
    deps_log_(new DepsLog()),
    disk_interface_(new RealDiskInterface()),
    is_explaining_(is_explaining),
    logger_(logger),
    start_time_millis_(GetTimeMillis())
 {
  bindings_.AddRule(&kPhonyRule);
  AddPool(&kDefaultPool);
  AddPool(&kConsolePool);
}

void State::AddPool(Pool* pool) {
  assert(LookupPool(pool->name()) == NULL);
  pools_[pool->name()] = pool;
}

Pool* State::LookupPool(const string& pool_name) {
  map<string, Pool*>::iterator i = pools_.find(pool_name);
  if (i == pools_.end())
    return NULL;
  return i->second;
}

Edge* State::AddEdge(const Rule* rule) {
  Edge* edge = new Edge();
  edge->rule_ = rule;
  edge->pool_ = &State::kDefaultPool;
  edge->env_ = &bindings_;
  edge->id_ = edges_.size();
  edges_.push_back(edge);
  return edge;
}

Node* State::GetNode(StringPiece path, uint64_t slash_bits) {
  Node* node = LookupNode(path);
  if (node)
    return node;
  node = new Node(path.AsString(), slash_bits);
  paths_[node->path()] = node;
  return node;
}

Node* State::LookupNode(StringPiece path) const {
  METRIC_RECORD("lookup node");
  Paths::const_iterator i = paths_.find(path);
  if (i != paths_.end())
    return i->second;
  return NULL;
}

void State::AddIn(Edge* edge, StringPiece path, uint64_t slash_bits) {
  Node* node = GetNode(path, slash_bits);
  edge->inputs_.push_back(node);
  node->AddOutEdge(edge);
}

bool State::AddOut(Edge* edge, StringPiece path, uint64_t slash_bits) {
  Node* node = GetNode(path, slash_bits);
  if (node->in_edge())
    return false;
  edge->outputs_.push_back(node);
  node->set_in_edge(edge);
  return true;
}

bool State::AddDefault(StringPiece path, string* err) {
  Node* node = LookupNode(path);
  if (!node) {
    *err = "unknown target '" + path.AsString() + "'";
    return false;
  }
  defaults_.push_back(node);
  return true;
}

bool State::IsPathDead(StringPiece s) const {
  Node* n = LookupNode(s);
  if (!n || !n->in_edge())
    return false;
  // Just checking n isn't enough: If an old output is both in the build log
  // and in the deps log, it will have a Node object in state_.  (It will also
  // have an in edge if one of its inputs is another output that's in the deps
  // log, but having a deps edge product an output that's input to another deps
  // edge is rare, and the first recompaction will delete all old outputs from
  // the deps log, and then a second recompaction will clear the build log,
  // which seems good enough for this corner case.)
  // Do keep entries around for files which still exist on disk, for
  // generators that want to use this information.
  string err;
  TimeStamp mtime = disk_interface_->Stat(s.AsString(), &err);
  if (mtime == -1) {
    // Log and ignore Stat() errors.
    logger_->OnMessage(Logger::ERROR, err);
  }
  return mtime == 0;
}

void State::Explain(const char* format, ...) const {
  if (!is_explaining_) {
    return;
  }

  va_list ap;
  va_start(ap, format);
  vfprintf(stdout, format, ap);
  va_end(ap);
}

vector<Node*> State::RootNodes(string* err) const {
  vector<Node*> root_nodes;
  // Search for nodes with no output.
  for (vector<Edge*>::const_iterator e = edges_.begin();
       e != edges_.end(); ++e) {
    for (vector<Node*>::const_iterator out = (*e)->outputs_.begin();
         out != (*e)->outputs_.end(); ++out) {
      if ((*out)->out_edges().empty())
        root_nodes.push_back(*out);
    }
  }

  if (!edges_.empty() && root_nodes.empty())
    *err = "could not determine root nodes of build graph";

  return root_nodes;
}

vector<Node*> State::DefaultNodes(string* err) const {
  return defaults_.empty() ? RootNodes(err) : defaults_;
}

void State::Reset() {
  for (Paths::iterator i = paths_.begin(); i != paths_.end(); ++i)
    i->second->ResetState();
  for (vector<Edge*>::iterator e = edges_.begin(); e != edges_.end(); ++e) {
    (*e)->outputs_ready_ = false;
    (*e)->deps_loaded_ = false;
    (*e)->mark_ = Edge::VisitNone;
  }
}

void State::Dump() {
  for (Paths::iterator i = paths_.begin(); i != paths_.end(); ++i) {
    Node* node = i->second;
    printf("%s %s [id:%d]\n",
           node->path().c_str(),
           node->status_known() ? (node->dirty() ? "dirty" : "clean")
                                : "unknown",
           node->id());
  }
  if (!pools_.empty()) {
    printf("resource_pools:\n");
    for (map<string, Pool*>::const_iterator it = pools_.begin();
         it != pools_.end(); ++it)
    {
      if (!it->second->name().empty()) {
        it->second->Dump();
      }
    }
  }
}

}  // namespace ninja
