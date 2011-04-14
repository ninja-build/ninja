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

#include "graph.h"
#include "ninja.h"
#include "util.h"
#include "build.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <assert.h>

Cleaner::Cleaner(State* state, const BuildConfig& config)
  : state_(state)
  , verbose_(config.verbosity == BuildConfig::VERBOSE || config.dry_run)
  , dry_run_(config.dry_run)
  , removed_()
{
}

bool Cleaner::RemoveFile(const string& path) {
  if (remove(path.c_str()) < 0) {
    switch (errno) {
      case ENOENT:
        return false;
      default:
        Error("remove(%s): %s", path.c_str(), strerror(errno));
        return false;
    }
  } else {
    return true;
  }
}

bool Cleaner::FileExists(const string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) < 0) {
    switch (errno) {
      case ENOENT:
        return false;
      default:
        Error("stat(%s): %s", path.c_str(), strerror(errno));
        return false;
    }
  } else {
    return true;
  }
}

void Cleaner::Report(const string& path) {
  if (verbose_)
    printf("Remove %s\n", path.c_str());
}

void Cleaner::Remove(const string& path) {
  if (!IsAlreadyRemoved(path)) {
    removed_.insert(path);
    if (dry_run_) {
      if (FileExists(path))
        Report(path);
    } else {
      if (RemoveFile(path))
        Report(path);
    }
  }
}

bool Cleaner::IsAlreadyRemoved(const string& path) {
  set<string>::iterator i = removed_.find(path);
  return (i != removed_.end());
}

void Cleaner::PrintHeader() {
  printf("Cleaning...");
  if (verbose_)
    printf("\n");
  else
    printf(" ");
}

void Cleaner::PrintFooter() {
  printf("%d files.\n", removed_.size());
}

void Cleaner::CleanAll() {
  PrintHeader();
  for (vector<Edge*>::iterator e = state_->edges_.begin();
       e != state_->edges_.end();
       ++e)
    for (vector<Node*>::iterator out_node = (*e)->outputs_.begin();
         out_node != (*e)->outputs_.end();
         ++out_node)
      Remove((*out_node)->file_->path_);
  PrintFooter();
}

void Cleaner::DoCleanTarget(Node* target) {
  if (target->in_edge_) {
    Remove(target->file_->path_);
    for (vector<Node*>::iterator n = target->in_edge_->inputs_.begin();
         n != target->in_edge_->inputs_.end();
         ++n) {
      DoCleanTarget(*n);
    }
  }
}

void Cleaner::CleanTarget(Node* target) {
  assert(target);

  PrintHeader();
  DoCleanTarget(target);
  PrintFooter();
}

int Cleaner::CleanTargets(int target_count, char* targets[]) {
  int status = 0;
  PrintHeader();
  for (int i = 0; i < target_count; ++i) {
    const char* target_name = targets[i];
    Node* target = state_->LookupNode(target_name);
    if (target) {
      if (verbose_)
        printf("Target %s\n", target_name);
      DoCleanTarget(target);
    } else {
      Error("unknown target '%s'", target_name);
      status = 1;
    }
  }
  PrintFooter();
  return status;
}

void Cleaner::DoCleanRule(const Rule* rule) {
  assert(rule);

  for (vector<Edge*>::iterator e = state_->edges_.begin();
       e != state_->edges_.end();
       ++e)
    if ((*e)->rule_->name_ == rule->name_)
      for (vector<Node*>::iterator out_node = (*e)->outputs_.begin();
           out_node != (*e)->outputs_.end();
           ++out_node)
        Remove((*out_node)->file_->path_);
}

void Cleaner::CleanRule(const Rule* rule) {
  assert(rule);

  PrintHeader();
  DoCleanRule(rule);
  PrintFooter();
}

int Cleaner::CleanRules(int rule_count, char* rules[]) {
  assert(rules);

  int status = 0;
  PrintHeader();
  for (int i = 0; i < rule_count; ++i) {
    const char* rule_name = rules[i];
    const Rule* rule = state_->LookupRule(rule_name);
    if (rule) {
      if (verbose_)
        printf("Rule %s\n", rule_name);
      DoCleanRule(rule);
    } else {
      Error("unknown rule '%s'", rule_name);
      status = 1;
    }
  }
  PrintFooter();
  return status;
}
