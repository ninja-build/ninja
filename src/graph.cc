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
#include "depfile_reader.h"
#include "disk_interface.h"
#include "metrics.h"
#include "parsers.h"
#include "state.h"
#include "util.h"

void Node::Stat(DiskInterface * disk_interface) {
  if (mtime_ == -1) {
    METRIC_RECORD("node stat");
    mtime_ = disk_interface->Stat(path_);
  }
}

bool Edge::RecomputeDirty(State* state, DiskInterface* disk_interface,
                          string* err) {
  bool dirty = false;
  outputs_ready_ = true;

  if (!rule_->depfile().empty()) {
    if (!LoadDepFile(state, disk_interface, err))
      return false;
  }

  // Visit all inputs; we're dirty if any of the inputs are dirty.
  TimeStamp most_recent_input = 1;
  for (vector<Node*>::iterator i = inputs_.begin(); i != inputs_.end(); ++i) {
    (*i)->Stat(disk_interface);
    if (!(*i)->IsVisited()) {
      (*i)->SetVisited();
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
    string command = EvaluateCommand(true);

    for (vector<Node*>::iterator i = outputs_.begin();
         i != outputs_.end(); ++i) {
      (*i)->Stat(disk_interface);
      (*i)->SetVisited();
      if (RecomputeOutputDirty(build_log, most_recent_input, command, *i)) {
        dirty = true;
        break;
      }
    }
  }

  // Finally, visit each output to mark off that we've visited it, and update
  // their dirty state if necessary.
  for (vector<Node*>::iterator i = outputs_.begin(); i != outputs_.end(); ++i) {
    (*i)->Stat(disk_interface);
    (*i)->SetVisited();
    if (dirty)
      (*i)->MarkDirty();
  }

  // If we're dirty, our outputs are normally not ready.  (It's possible to be
  // clean but still not be ready in the presence of order-only inputs.)
  // But phony edges with no inputs have nothing to do, so are always ready.
  if (dirty && !(is_phony() && inputs_.empty()))
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

string Edge::EvaluateCommand(bool incl_rsp_file) {
  EdgeEnv env(this);
  string command = rule_->command().Evaluate(&env);
  if (incl_rsp_file && HasRspFile()) 
    command += ";rspfile=" + GetRspFileContent();
  return command;
}

string Edge::EvaluateDepFile() {
  EdgeEnv env(this);
  return rule_->depfile().Evaluate(&env);
}

string Edge::EvaluateDepFileGroup() {
  EdgeEnv env(this);
  return rule_->depfile_group().Evaluate(&env);
}

string Edge::GetDescription() {
  EdgeEnv env(this);
  return rule_->description().Evaluate(&env);
}

bool Edge::HasRspFile() {
  return !rule_->rspfile().empty();
}

string Edge::GetRspFile() {
  EdgeEnv env(this);
  return rule_->rspfile().Evaluate(&env);
}

string Edge::GetRspFileContent() {
  EdgeEnv env(this);
  return rule_->rspfile_content().Evaluate(&env);
}

bool Edge::depfile_group_up_to_date(State* state, DiskInterface* disk_interface, 
                                    const string& depfile_group_path, string* err) {
  // determine the most recent timestamp associated with the edge
  TimeStamp most_recent_output = 1;      
  for (vector<Node *>::iterator node = outputs_.begin(); node != outputs_.end(); node++) {
    // stat the output file, but don't mark it as processed
    (*node)->Stat(disk_interface);
    most_recent_output = max((*node)->mtime(), most_recent_output);
    
    // if this is a restat rule, check the log for this output
    if (rule_->restat() && state->build_log_) {
      BuildLog::LogEntry* entry = state->build_log_->LookupByOutput((*node)->path());
      if (entry) 
        most_recent_output = max(entry->restat_mtime, most_recent_output);
    }
  }

  // stat the grouped depfile (assuming its part of the graph)
  Node * depfile_group = state->GetNode(depfile_group_path);
  if (NULL == depfile_group) {
    *err = "Unable to acquire a Node object for " + depfile_group_path;
    return false;
  }
  
  // decide which depfile to use
  depfile_group->Stat(disk_interface);
  if (depfile_group->exists() && depfile_group->mtime() >= most_recent_output) {
    return true;
  } else {
    return false;
  }
}

bool Edge::LoadDepFile(State* state, DiskInterface* disk_interface,
                       string* err) {
  METRIC_RECORD("depfile load");  
  string depfile_path = EvaluateDepFile();  
  bool usingDepFileGroup = false;
  if (!rule_->depfile_group().empty()) {
    // here we're using a grouped depfile, need to make sure it's up to date    
    string depfile_group_path = EvaluateDepFileGroup();
    usingDepFileGroup = depfile_group_up_to_date(state, disk_interface, depfile_group_path, err);   
    if (!err->empty())
      return false; 
    if (usingDepFileGroup)
      depfile_path = depfile_group_path;
  }
  
  // Load the depfile (from disk or cache)
  DepfileReader reader;
  if (usingDepFileGroup)
    reader.ReadGroup(depfile_path, outputs_[0]->path(), disk_interface, err);
  else
    reader.Read(depfile_path, outputs_[0]->path(), disk_interface, err);
  if (!err->empty()) {
    return false;
  }

  // TODO a better approach?
  if (NULL == reader.Parser()) {
    // no parsed contents
    return true;
  }  

  DepfileParser & depfile = *reader.Parser(); 

  inputs_.insert(inputs_.end() - order_only_deps_, depfile.ins().size(), 0);
  implicit_deps_ += depfile.ins().size();
  vector<Node*>::iterator implicit_dep =
    inputs_.end() - order_only_deps_ - depfile.ins().size();

  // Add all its in-edges.
  for (vector<StringPiece>::iterator i = depfile.ins().begin();
       i != depfile.ins().end(); ++i, ++implicit_dep) {
    if (!CanonicalizePath(const_cast<char*>(i->str_), &i->len_, err))
      return false;

    Node* node = state->GetNode(*i);
    *implicit_dep = node;
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
