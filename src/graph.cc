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
#include "depfile_parser.h"
#include "deplist.h"
#include "disk_interface.h"
#include "metrics.h"
#include "parsers.h"
#include "state.h"
#include "util.h"

bool Node::Stat(DiskInterface* disk_interface) {
  METRIC_RECORD("node stat");
  mtime_ = disk_interface->Stat(path_);
  return mtime_ > 0;
}

bool Edge::RecomputeDirty(State* state, DiskInterface* disk_interface,
                          string* err) {
  bool dirty = false;
  outputs_ready_ = true;

  if (!rule_->depfile().empty()) {
    if (!LoadDepFile(state, disk_interface, err))
      return false;
  } else if (!rule_->deplist().empty()) {
    if (!LoadDepList(state, disk_interface, err))
      return false;
  }

  // Visit all inputs; we're dirty if any of the inputs are dirty.
  TimeStamp most_recent_input = 1;
  for (vector<Node*>::iterator i = inputs_.begin(); i != inputs_.end(); ++i) {
    if ((*i)->StatIfNecessary(disk_interface)) {
      if (Edge* edge = (*i)->in_edge()) {
        if (!edge->RecomputeDirty(state, disk_interface, err))
          return false;
      } else {
        // This input has no in-edge; it is dirty if it is missing.
        (*i)->set_dirty(!(*i)->exists());
      }
    }

    // If an input is not ready, neither are our outputs.
    if (Edge* edge = (*i)->in_edge()) {
      if (!edge->outputs_ready_)
        outputs_ready_ = false;
    }

    if (!is_order_only(i - inputs_.begin())) {
      // If a regular input is dirty (or missing), we're dirty.
      // Otherwise consider mtime.
      if ((*i)->dirty()) {
        dirty = true;
      } else {
        if ((*i)->mtime() > most_recent_input)
          most_recent_input = (*i)->mtime();
      }
    }
  }

  // We may also be dirty due to output state: missing outputs, out of
  // date outputs, etc.  Visit all outputs and determine whether they're dirty.
  if (!dirty) {
    BuildLog* build_log = state ? state->build_log_ : 0;
    string command = EvaluateCommand();

    for (vector<Node*>::iterator i = outputs_.begin();
         i != outputs_.end(); ++i) {
      (*i)->StatIfNecessary(disk_interface);
      if (RecomputeOutputDirty(build_log, most_recent_input, command, *i)) {
        dirty = true;
        break;
      }
    }
  }

  // Finally, visit each output to mark off that we've visited it, and update
  // their dirty state if necessary.
  for (vector<Node*>::iterator i = outputs_.begin(); i != outputs_.end(); ++i) {
    (*i)->StatIfNecessary(disk_interface);
    if (dirty)
      (*i)->MarkDirty();
  }

  // If we're dirty, our outputs are not ready.  (It's possible to be
  // clean but still have not be ready in the presence of order-only
  // inputs.)
  if (dirty)
    outputs_ready_ = false;

  return true;
}

bool Edge::RecomputeOutputDirty(BuildLog* build_log,
                                TimeStamp most_recent_input,
                                const string& command, Node* output) {
  if (is_phony()) {
    // Phony edges don't write any output.  Outputs are only dirty if
    // there are no inputs and we're missing the output.
    return inputs_.empty() && !output->exists();
  }

  BuildLog::LogEntry* entry = 0;

  // Dirty if we're missing the output.
  if (!output->exists())
    return true;

  // Dirty if the output is older than the input.
  if (output->mtime() < most_recent_input) {
    // If this is a restat rule, we may have cleaned the output with a restat
    // rule in a previous run and stored the most recent input mtime in the
    // build log.  Use that mtime instead, so that the file will only be
    // considered dirty if an input was modified since the previous run.
    if (rule_->restat() && build_log &&
        (entry = build_log->LookupByOutput(output->path()))) {
      if (entry->restat_mtime < most_recent_input)
        return true;
    } else {
      return true;
    }
  }

  // May also be dirty due to the command changing since the last build.
  // But if this is a generator rule, the command changing does not make us
  // dirty.
  if (!rule_->generator() && build_log &&
      (entry || (entry = build_log->LookupByOutput(output->path())))) {
    if (command != entry->command)
      return true;
  }

  return false;
}

bool Edge::AllInputsReady() const {
  for (vector<Node*>::const_iterator i = inputs_.begin();
       i != inputs_.end(); ++i) {
    if ((*i)->in_edge() && !(*i)->in_edge()->outputs_ready())
      return false;
  }
  return true;
}

/// An Env for an Edge, providing $in and $out.
struct EdgeEnv : public Env {
  EdgeEnv(Edge* edge) : edge_(edge) {}
  virtual string LookupVariable(const string& var);

  /// Given a span of Nodes, construct a list of paths suitable for a command
  /// line.  XXX here is where shell-escaping of e.g spaces should happen.
  string MakePathList(vector<Node*>::iterator begin,
                      vector<Node*>::iterator end);

  Edge* edge_;
};

string EdgeEnv::LookupVariable(const string& var) {
  if (var == "in") {
    int explicit_deps_count = edge_->inputs_.size() - edge_->implicit_deps_ -
      edge_->order_only_deps_;
    return MakePathList(edge_->inputs_.begin(),
                        edge_->inputs_.begin() + explicit_deps_count);
  } else if (var == "out") {
    return MakePathList(edge_->outputs_.begin(),
                        edge_->outputs_.end());
  } else if (edge_->env_) {
    return edge_->env_->LookupVariable(var);
  } else {
    // XXX shoudl we warn here?
    return string();
  }
}

string EdgeEnv::MakePathList(vector<Node*>::iterator begin,
                             vector<Node*>::iterator end) {
  string result;
  for (vector<Node*>::iterator i = begin; i != end; ++i) {
    if (!result.empty())
      result.push_back(' ');
    const string& path = (*i)->path();
    if (path.find(" ") != string::npos) {
      result.append("\"");
      result.append(path);
      result.append("\"");
    } else {
      result.append(path);
    }
  }
  return result;
}

string Edge::EvaluateCommand() {
  EdgeEnv env(this);
  return rule_->command().Evaluate(&env);
}

string Edge::EvaluateDepFile() {
  EdgeEnv env(this);
  return rule_->depfile().Evaluate(&env);
}

string Edge::GetDescription() {
  EdgeEnv env(this);
  return rule_->description().Evaluate(&env);
}

Node* Edge::GetDepNode(State* state, StringPiece path) {
  Node* node = state->GetNode(path.AsString());  // XXX remove conversion
  node->AddOutEdge(this);

  // If we don't have a edge that generates this input already,
  // create one; this makes us not abort if the input is missing,
  // but instead will rebuild in that circumstance.
  if (!node->in_edge()) {
    Edge* phony_edge = state->AddEdge(&State::kPhonyRule);
    node->set_in_edge(phony_edge);
    phony_edge->outputs_.push_back(node);

    // RecomputeDirty might not be called for phony_edge if a previous call
    // to RecomputeDirty had caused the file to be stat'ed.  Because previous
    // invocations of RecomputeDirty would have seen this node without an
    // input edge (and therefore ready), we have to set outputs_ready_ to true
    // to avoid a potential stuck build.  If we do call RecomputeDirty for
    // this node, it will simply set outputs_ready_ to the correct value.
    phony_edge->outputs_ready_ = true;
  }

  return node;
}

bool Edge::LoadDepFile(State* state, DiskInterface* disk_interface,
                       string* err) {
  METRIC_RECORD("depfile load");
  string path = EvaluateDepFile();
  string content = disk_interface->ReadFile(path, err);
  if (!err->empty())
    return false;
  if (content.empty())
    return true;

  DepfileParser depfile;
  string depfile_err;
  if (!depfile.Parse(&content, &depfile_err)) {
    *err = path + ": " + depfile_err;
    return false;
  }

  // Check that this depfile matches our output.
  StringPiece opath = StringPiece(outputs_[0]->path());
  if (opath != depfile.out_) {
    *err = "expected depfile '" + path + "' to mention '" +
      outputs_[0]->path() + "', got '" + depfile.out_.AsString() + "'";
    return false;
  }

  inputs_.insert(inputs_.end() - order_only_deps_, depfile.ins_.size(), 0);
  implicit_deps_ += depfile.ins_.size();
  vector<Node*>::iterator implicit_dep =
    inputs_.end() - order_only_deps_ - depfile.ins_.size();

  // Add all its in-edges.
  for (vector<StringPiece>::iterator i = depfile.ins_.begin();
       i != depfile.ins_.end(); ++i, ++implicit_dep) {
    string path(i->str_, i->len_);
    if (!CanonicalizePath(&path, err))
      return false;
    *implicit_dep = GetDepNode(state, path);
  }

  return true;
}

bool Edge::LoadDepList(State* state, DiskInterface* disk_interface,
                       string* err) {
  METRIC_RECORD("deplist load");

  EdgeEnv env(this);
  string path = rule_->deplist().Evaluate(&env);

  string content = disk_interface->ReadFile(path, err);
  if (!err->empty())
    return false;
  if (content.empty())
    return true;

  vector<StringPiece> deps;
  if (!Deplist::Load(content, &deps, err))
    return false;

  inputs_.insert(inputs_.end() - order_only_deps_, deps.size(), 0);
  implicit_deps_ += deps.size();
  vector<Node*>::iterator implicit_dep =
    inputs_.end() - order_only_deps_ - deps.size();

  // Add all its in-edges.
  for (vector<StringPiece>::iterator i = deps.begin();
       i != deps.end(); ++i, ++implicit_dep) {
    *implicit_dep = GetDepNode(state, *i);
  }

  return true;
}

void Edge::Dump() {
  printf("[ ");
  for (vector<Node*>::iterator i = inputs_.begin(); i != inputs_.end(); ++i) {
    printf("%s ", (*i)->path().c_str());
  }
  printf("--%s-> ", rule_->name().c_str());
  for (vector<Node*>::iterator i = outputs_.begin(); i != outputs_.end(); ++i) {
    printf("%s ", (*i)->path().c_str());
  }
  printf("]\n");
}

bool Edge::is_phony() const {
  return rule_ == &State::kPhonyRule;
}
