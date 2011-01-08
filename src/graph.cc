#include "graph.h"

#include <stdio.h>

#include "build_log.h"
#include "ninja.h"
#include "parsers.h"

void FileStat::Touch(int mtime) {
  mtime_ = mtime;
  if (node_)
    node_->MarkDirty();
}

bool FileStat::Stat(DiskInterface* disk_interface) {
  mtime_ = disk_interface->Stat(path_);
  return mtime_ > 0;
}

void Node::MarkDirty() {
  if (dirty_)
    return;  // We already know.

  dirty_ = true;
  MarkDependentsDirty();
}

void Node::MarkDependentsDirty() {
  for (vector<Edge*>::iterator i = out_edges_.begin(); i != out_edges_.end(); ++i)
    (*i)->MarkDirty(this);
}

bool Edge::RecomputeDirty(State* state, DiskInterface* disk_interface,
                          string* err) {
  bool dirty = false;

  if (!rule_->depfile_.empty()) {
    if (!LoadDepFile(state, disk_interface, err))
      return false;
  }

  time_t most_recent_input = 1;
  for (vector<Node*>::iterator i = inputs_.begin(); i != inputs_.end(); ++i) {
    if ((*i)->file_->StatIfNecessary(disk_interface)) {
      if (Edge* edge = (*i)->in_edge_) {
        if (!edge->RecomputeDirty(state, disk_interface, err))
          return false;
      } else {
        // This input has no in-edge; it is dirty if it is missing.
        // But it's ok for implicit deps to be missing.
        if (!is_implicit(i - inputs_.begin()))
          (*i)->dirty_ = !(*i)->file_->exists();
      }
    }

    if (is_order_only(i - inputs_.begin())) {
      // Order-only deps only make us dirty if they're missing.
      if (!(*i)->file_->exists())
        dirty = true;
      continue;
    }

    // If a regular input is dirty (or missing), we're dirty.
    // Otherwise consider mtime.
    if ((*i)->dirty_) {
      dirty = true;
    } else {
      if ((*i)->file_->mtime_ > most_recent_input)
        most_recent_input = (*i)->file_->mtime_;
    }
  }

  string command = EvaluateCommand();

  assert(!outputs_.empty());
  for (vector<Node*>::iterator i = outputs_.begin(); i != outputs_.end(); ++i) {
    // We may have other outputs, that our input-recursive traversal hasn't hit
    // yet (or never will).  Stat them if we haven't already.
    (*i)->file_->StatIfNecessary(disk_interface);

    // Output is dirty if we're dirty, we're missing the output,
    // or if it's older than the most recent input mtime.
    if (dirty || !(*i)->file_->exists() ||
        (*i)->file_->mtime_ < most_recent_input) {
      (*i)->dirty_ = true;
    } else {
      // May also be dirty due to the command changing since the last build.
      BuildLog::LogEntry* entry;
      if (state->build_log_ &&
          (entry = state->build_log_->LookupByOutput((*i)->file_->path_))) {
        if (command != entry->command)
          (*i)->dirty_ = true;
      }
    }
  }
  return true;
}

void Edge::MarkDirty(Node* node) {
  if (rule_ == &State::kPhonyRule)
    return;

  vector<Node*>::iterator i = find(inputs_.begin(), inputs_.end(), node);
  if (i == inputs_.end())
    return;
  if (i - inputs_.begin() >= ((int)inputs_.size()) - order_only_deps_)
    return;  // Order-only deps don't cause us to become dirty.
  for (i = outputs_.begin(); i != outputs_.end(); ++i)
    (*i)->MarkDirty();
}

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
      result = edge_->outputs_[0]->file_->path_;
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

bool Edge::LoadDepFile(State* state, DiskInterface* disk_interface, string* err) {
  EdgeEnv env(this);
  string path = rule_->depfile_.Evaluate(&env);

  string content = disk_interface->ReadFile(path, err);
  if (!err->empty())
    return false;
  if (content.empty())
    return true;

  MakefileParser makefile;
  if (!makefile.Parse(content, err))
    return false;

  // Check that this depfile matches our output.
  if (outputs_.size() != 1) {
    *err = "expected only one output";
    return false;
  }
  if (outputs_[0]->file_->path_ != makefile.out_) {
    *err = "expected makefile to mention '" + outputs_[0]->file_->path_ + "', "
           "got '" + makefile.out_ + "'";
    return false;
  }

  // Add all its in-edges.
  for (vector<string>::iterator i = makefile.ins_.begin();
       i != makefile.ins_.end(); ++i) {
    Node* node = state->GetNode(*i);
    for (vector<Node*>::iterator j = inputs_.begin(); j != inputs_.end(); ++j) {
      if (*j == node) {
        node = NULL;
        break;
      }
    }
    if (node) {
      inputs_.insert(inputs_.end() - order_only_deps_, node);
      node->out_edges_.push_back(this);
      ++implicit_deps_;
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

