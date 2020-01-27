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

#include "clean.h"

#include <assert.h>

#include <sstream>

#include "disk_interface.h"
#include "graph.h"
#include "state.h"
#include "util.h"

namespace ninja {
Cleaner::Cleaner(State* state,
                 Logger* logger,
                 const BuildConfig& config,
                 DiskInterface* disk_interface)
  : cleaned_(),
    cleaned_files_count_(0),
    config_(config),
    disk_interface_(disk_interface),
    dyndep_loader_(state, disk_interface),
    logger_(logger),
    removed_(),
    state_(state),
    status_(0) {
}

int Cleaner::RemoveFile(const std::string& path) {
  return disk_interface_->RemoveFile(path);
}

bool Cleaner::FileExists(const std::string& path) {
  std::string err;
  TimeStamp mtime = disk_interface_->Stat(path, &err);
  if (mtime == -1)
    Error("%s", err.c_str());
  return mtime > 0;  // Treat Stat() errors as "file does not exist".
}

void Cleaner::Report(const std::string& path) {
  ++cleaned_files_count_;
  if (IsVerbose()) {
    std::ostringstream buffer;
    buffer << "Remove " << path << std::endl;
    logger_->Info(buffer.str());
  }
}

void Cleaner::Remove(const std::string& path) {
  if (!IsAlreadyRemoved(path)) {
    removed_.insert(path);
    if (config_.dry_run) {
      if (FileExists(path))
        Report(path);
    } else {
      int ret = RemoveFile(path);
      if (ret == 0)
        Report(path);
      else if (ret == -1)
        status_ = 1;
    }
  }
}

bool Cleaner::IsAlreadyRemoved(const std::string& path) {
  set<std::string>::iterator i = removed_.find(path);
  return (i != removed_.end());
}

void Cleaner::RemoveEdgeFiles(Edge* edge) {
  std::string depfile = edge->GetUnescapedDepfile();
  if (!depfile.empty())
    Remove(depfile);

  std::string rspfile = edge->GetUnescapedRspfile();
  if (!rspfile.empty())
    Remove(rspfile);
}

void Cleaner::PrintHeader() {
  if (config_.verbosity == BuildConfig::QUIET)
    return;
  if (IsVerbose())
    logger_->Info("Cleaning...\n");
  else
    logger_->Info("Cleaning... ");
}

void Cleaner::PrintFooter() {
  if (config_.verbosity == BuildConfig::QUIET)
    return;
  std::ostringstream buffer;
  buffer << "Cleaned " << cleaned_files_count_ << " files" << std::endl;
  logger_->Info(buffer.str());
}

int Cleaner::CleanAll(bool generator) {
  Reset();
  PrintHeader();
  LoadDyndeps();
  for (vector<Edge*>::iterator e = state_->edges_.begin();
       e != state_->edges_.end(); ++e) {
    // Do not try to remove phony targets
    if ((*e)->is_phony())
      continue;
    // Do not remove generator's files unless generator specified.
    if (!generator && (*e)->GetBindingBool("generator"))
      continue;
    for (vector<Node*>::iterator out_node = (*e)->outputs_.begin();
         out_node != (*e)->outputs_.end(); ++out_node) {
      Remove((*out_node)->path());
    }

    RemoveEdgeFiles(*e);
  }
  PrintFooter();
  return status_;
}

void Cleaner::DoCleanTarget(Node* target) {
  if (Edge* e = target->in_edge()) {
    // Do not try to remove phony targets
    if (!e->is_phony()) {
      Remove(target->path());
      RemoveEdgeFiles(e);
    }
    for (vector<Node*>::iterator n = e->inputs_.begin(); n != e->inputs_.end();
         ++n) {
      Node* next = *n;
      // call DoCleanTarget recursively if this node has not been visited
      if (cleaned_.count(next) == 0) {
        DoCleanTarget(next);
      }
    }
  }

  // mark this target to be cleaned already
  cleaned_.insert(target);
}

int Cleaner::CleanTarget(Node* target) {
  assert(target);

  Reset();
  PrintHeader();
  LoadDyndeps();
  DoCleanTarget(target);
  PrintFooter();
  return status_;
}

int Cleaner::CleanTarget(const char* target) {
  assert(target);

  Reset();
  Node* node = state_->LookupNode(target);
  if (node) {
    CleanTarget(node);
  } else {
    Error("unknown target '%s'", target);
    status_ = 1;
  }
  return status_;
}

int Cleaner::CleanTargets(const std::vector<std::string>& targets) {
  Reset();
  PrintHeader();
  LoadDyndeps();
  for (size_t i = 0; i < targets.size(); ++i) {
    std::string target_name = targets[i];
    uint64_t slash_bits;
    std::string err;
    if (!CanonicalizePath(&target_name, &slash_bits, &err)) {
      Error("failed to canonicalize '%s': %s", target_name.c_str(), err.c_str());
      status_ = 1;
    } else {
      Node* target = state_->LookupNode(target_name);
      if (target) {
        if (IsVerbose()) {
          std::ostringstream buffer;
          buffer << "Target " << target_name << std::endl;
          logger_->Info(buffer.str());
        }
        DoCleanTarget(target);
      } else {
        Error("unknown target '%s'", target_name.c_str());
        status_ = 1;
      }
    }
  }
  PrintFooter();
  return status_;
}

void Cleaner::DoCleanRule(const Rule* rule) {
  assert(rule);

  for (vector<Edge*>::iterator e = state_->edges_.begin();
       e != state_->edges_.end(); ++e) {
    if ((*e)->rule().name() == rule->name()) {
      for (vector<Node*>::iterator out_node = (*e)->outputs_.begin();
           out_node != (*e)->outputs_.end(); ++out_node) {
        Remove((*out_node)->path());
        RemoveEdgeFiles(*e);
      }
    }
  }
}

int Cleaner::CleanRule(const Rule* rule) {
  assert(rule);

  Reset();
  PrintHeader();
  LoadDyndeps();
  DoCleanRule(rule);
  PrintFooter();
  return status_;
}

int Cleaner::CleanRule(const char* rule) {
  assert(rule);

  Reset();
  const Rule* r = state_->bindings_.LookupRule(rule);
  if (r) {
    CleanRule(r);
  } else {
    Error("unknown rule '%s'", rule);
    status_ = 1;
  }
  return status_;
}

int Cleaner::CleanRules(const std::vector<std::string>& rules) {
  assert(rules);

  Reset();
  PrintHeader();
  LoadDyndeps();
  for (size_t i = 0; i < rules.size(); ++i) {
    std::string rule_name = rules[i];
    const Rule* rule = state_->bindings_.LookupRule(rule_name);
    if (rule) {
      if (IsVerbose()) {
        std::ostringstream buffer;
        buffer << "Rule " << rule_name << std::endl;
        logger_->Info(buffer.str());
      }
      DoCleanRule(rule);
    } else {
      Error("unknown rule '%s'", rule_name.c_str());
      status_ = 1;
    }
  }
  PrintFooter();
  return status_;
}

void Cleaner::Reset() {
  status_ = 0;
  cleaned_files_count_ = 0;
  removed_.clear();
  cleaned_.clear();
}

void Cleaner::LoadDyndeps() {
  // Load dyndep files that exist, before they are cleaned.
  for (vector<Edge*>::iterator e = state_->edges_.begin();
       e != state_->edges_.end(); ++e) {
    if (Node* dyndep = (*e)->dyndep_) {
      // Capture and ignore errors loading the dyndep file.
      // We clean as much of the graph as we know.
      std::string err;
      dyndep_loader_.LoadDyndeps(dyndep, &err);
    }
  }
}
}  // namespace ninja
