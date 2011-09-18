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

#include "graph.h"

#include <assert.h>
#include <stdio.h>

#include "build_log.h"
#include "disk_interface.h"
#include "parsers.h"
#include "state.h"
#include "util.h"

bool FileStat::Stat(DiskInterface* disk_interface) {
  mtime_ = disk_interface->Stat(path_);
  return mtime_ > 0;
}

bool Edge::RecomputeDirty(State* state, DiskInterface* disk_interface,
                          string* err) {
  bool dirty = false;

  if (!rule_->depfile_.empty()) {
    if (!LoadDepFile(state, disk_interface, err))
      return false;
  }

  outputs_ready_ = true;

  time_t most_recent_input = 1;
  for (vector<Node*>::iterator i = inputs_.begin(); i != inputs_.end(); ++i) {
    if ((*i)->file_->StatIfNecessary(disk_interface)) {
      if (Edge* edge = (*i)->in_edge_) {
        if (!edge->RecomputeDirty(state, disk_interface, err))
          return false;
      } else {
        // This input has no in-edge; it is dirty if it is missing.
        (*i)->dirty_ = !(*i)->file_->exists();
      }
    }

    // If an input is not ready, neither are our outputs.
    if (Edge* edge = (*i)->in_edge_)
      if (!edge->outputs_ready_)
        outputs_ready_ = false;

    if (!is_order_only(i - inputs_.begin())) {
       // If a regular input is dirty (or missing), we're dirty.
       // Otherwise consider mtime.
       if ((*i)->dirty_) {
         dirty = true;
       } else {
         if ((*i)->file_->mtime_ > most_recent_input)
           most_recent_input = (*i)->file_->mtime_;
       }
    }
  }

  BuildLog* build_log = state ? state->build_log_ : 0;
  string command = EvaluateCommand();

  assert(!outputs_.empty());
  for (vector<Node*>::iterator i = outputs_.begin(); i != outputs_.end(); ++i) {
    // We may have other outputs that our input-recursive traversal hasn't hit
    // yet (or never will).  Stat them if we haven't already to mark that we've
    // visited their dependents.
    (*i)->file_->StatIfNecessary(disk_interface);

    RecomputeOutputDirty(build_log, most_recent_input, dirty, command, *i);
    if ((*i)->dirty_)
      outputs_ready_ = false;
  }

  return true;
}

void Edge::RecomputeOutputDirty(BuildLog* build_log, time_t most_recent_input,
                                bool dirty, const string& command,
                                Node* output) {
  if (is_phony()) {
    // Phony edges don't write any output.
    // They're only dirty if an input is dirty, or if there are no inputs
    // and we're missing the output.
    if (dirty)
      output->dirty_ = true;
    else if (inputs_.empty() && !output->file_->exists())
      output->dirty_ = true;
    return;
  }

  // Output is dirty if we're dirty, we're missing the output,
  // or if it's older than the most recent input mtime.
  if (dirty || !output->file_->exists() ||
      output->file_->mtime_ < most_recent_input) {
    output->dirty_ = true;
  } else {
    // May also be dirty due to the command changing since the last build.
    // But if this is a generator rule, the command changing does not make us
    // dirty.
    BuildLog::LogEntry* entry;
    if (!rule_->generator_ && build_log &&
        (entry = build_log->LookupByOutput(output->file_->path_))) {
      if (command != entry->command)
        output->dirty_ = true;
    }
  }
}

/// An Env for an Edge, providing $in and $out.
struct EdgeEnv : public Env {
  EdgeEnv(Edge* edge) : edge_(edge) {}
  virtual string LookupVariable(const string& var) {
    string result;
    if (var == "in") {
      int explicit_deps = edge_->inputs_.size() - edge_->implicit_deps_ -
          edge_->order_only_deps_;
      for (vector<Node*>::iterator i = edge_->inputs_.begin();
           i != edge_->inputs_.end() && explicit_deps; ++i, --explicit_deps) {
        if (!result.empty())
          result.push_back(' ');
        result.append((*i)->file_->path_);
      }
    } else if (var == "out") {
      for (vector<Node*>::iterator i = edge_->outputs_.begin();
           i != edge_->outputs_.end(); ++i) {
        if (!result.empty())
          result.push_back(' ');
        result.append((*i)->file_->path_);
      }
    } else if (edge_->env_) {
      return edge_->env_->LookupVariable(var);
    }
    return result;
  }
  Edge* edge_;
};

string Edge::EvaluateCommand() {
  EdgeEnv env(this);
  return rule_->command_.Evaluate(&env);
}

string Edge::GetDescription() {
  EdgeEnv env(this);
  return rule_->description_.Evaluate(&env);
}

bool Edge::LoadDepFile(State* state, DiskInterface* disk_interface,
                       string* err) {
  EdgeEnv env(this);
  string path = rule_->depfile_.Evaluate(&env);

  string content = disk_interface->ReadFile(path, err);
  if (!err->empty())
    return false;
  if (content.empty())
    return true;

  MakefileParser makefile;
  string makefile_err;
  if (!makefile.Parse(content, &makefile_err)) {
    *err = path + ": " + makefile_err;
    return false;
  }

  // Check that this depfile matches our output.
  StringPiece opath = StringPiece(outputs_[0]->file_->path_);
  if (opath != makefile.out_) {
    *err = "expected makefile to mention '" + outputs_[0]->file_->path_ + "', "
        "got '" + makefile.out_.AsString() + "'";
    return false;
  }

  inputs_.insert(inputs_.end() - order_only_deps_, makefile.ins_.size(), 0);
  implicit_deps_ += makefile.ins_.size();
  vector<Node*>::iterator implicit_dep =
    inputs_.end() - order_only_deps_ - makefile.ins_.size();

  // Add all its in-edges.
  for (vector<StringPiece>::iterator i = makefile.ins_.begin();
       i != makefile.ins_.end(); ++i, ++implicit_dep) {
    string path(i->str_, i->len_);
    if (!CanonicalizePath(&path, err))
      return false;

    Node* node = state->GetNode(path);
    *implicit_dep = node;
    node->out_edges_.push_back(this);

    // If we don't have a edge that generates this input already,
    // create one; this makes us not abort if the input is missing,
    // but instead will rebuild in that circumstance.
    if (!node->in_edge_) {
      Edge* phony_edge = state->AddEdge(&State::kPhonyRule);
      node->in_edge_ = phony_edge;
      phony_edge->outputs_.push_back(node);

      // RecomputeDirty might not be called for phony_edge if a previous call
      // to RecomputeDirty had caused the file to be stat'ed.  Because previous
      // invocations of RecomputeDirty would have seen this node without an
      // input edge (and therefore ready), we have to set outputs_ready_ to true
      // to avoid a potential stuck build.  If we do call RecomputeDirty for
      // this node, it will simply set outputs_ready_ to the correct value.
      phony_edge->outputs_ready_ = true;
    }
  }

  return true;
}

void Edge::Dump() {
  printf("[ ");
  for (vector<Node*>::iterator i = inputs_.begin(); i != inputs_.end(); ++i) {
    printf("%s ", (*i)->file_->path_.c_str());
  }
  printf("--%s-> ", rule_->name_.c_str());
  for (vector<Node*>::iterator i = outputs_.begin(); i != outputs_.end(); ++i) {
    printf("%s ", (*i)->file_->path_.c_str());
  }
  printf("]\n");
}

bool Edge::is_phony() const {
  return rule_ == &State::kPhonyRule;
}
