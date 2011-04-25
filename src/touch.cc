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

#include "touch.h"

#include "graph.h"
#include "ninja.h"
#include "util.h"
#include "build.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utime.h>
#include <assert.h>

Toucher::Toucher(State* state, const BuildConfig& config)
  : state_(state)
  , verbose_(config.verbosity == BuildConfig::VERBOSE || config.dry_run)
  , dry_run_(config.dry_run)
  , touched_()
{
}

bool Toucher::TouchFile(const string& path) {
  if(utime(path.c_str(), NULL) < 0) {
    switch (errno) {
      case ENOENT:
        return false;
      default:
        Error("utime(%s): %s", path.c_str(), strerror(errno));
        return false;
    }
  } else {
    return true;
  }
}

bool Toucher::FileExists(const string& path) {
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

void Toucher::Report(const string& path) {
  if (verbose_)
    printf("Touch %s\n", path.c_str());
}

void Toucher::Touch(const string& path) {
  if (!IsAlreadyTouched(path)) {
    touched_.insert(path);
    if (dry_run_) {
      if (FileExists(path))
        Report(path);
    } else {
      if (TouchFile(path))
        Report(path);
    }
  }
}

bool Toucher::IsAlreadyTouched(const string& path) {
  set<string>::iterator i = touched_.find(path);
  return (i != touched_.end());
}

void Toucher::PrintHeader() {
  printf("Touching...");
  if (verbose_)
    printf("\n");
  else
    printf(" ");
}

void Toucher::PrintFooter() {
  printf("%d files.\n", touched_.size());
}

void Toucher::TouchAll() {
  PrintHeader();
  for (vector<Edge*>::iterator e = state_->edges_.begin();
       e != state_->edges_.end();
       ++e)
    for (vector<Node*>::iterator inp_node = (*e)->inputs_.begin();
         inp_node != (*e)->inputs_.end();
         ++inp_node)
      // If they have no input edges then they are leaves so source file.
      if (!(*inp_node)->in_edge_)
        Touch((*inp_node)->file_->path_);
  PrintFooter();
}

void Toucher::DoTouchTarget(Node* target) {
  assert(target);

  if (target->in_edge_) {
    for (vector<Node*>::iterator n = target->in_edge_->inputs_.begin();
         n != target->in_edge_->inputs_.end();
         ++n) {
      DoTouchTarget(*n);
    }
  } else {
    Touch(target->file_->path_);
  }
}

void Toucher::TouchTarget(Node* target) {
  assert(target);

  PrintHeader();
  DoTouchTarget(target);
  PrintFooter();
}

int Toucher::TouchTarget(const char* target) {
  assert(target);

  return TouchTargets(1, &target);
}

int Toucher::TouchTargets(int target_count, const char* targets[]) {
  int status = 0;
  PrintHeader();
  for (int i = 0; i < target_count; ++i) {
    const char* target_name = targets[i];
    Node* target = state_->LookupNode(target_name);
    if (target) {
      if (verbose_)
        printf("Target %s\n", target_name);
      DoTouchTarget(target);
    } else {
      Error("unknown target '%s'", target_name);
      status = 1;
    }
  }
  PrintFooter();
  return status;
}

void Toucher::DoTouchRule(const Rule* rule) {
  assert(rule);

  for (vector<Edge*>::iterator e = state_->edges_.begin();
       e != state_->edges_.end();
       ++e)
    if ((*e)->rule_->name_ == rule->name_)
      for (vector<Node*>::iterator inp_node = (*e)->inputs_.begin();
           inp_node != (*e)->inputs_.end();
           ++inp_node)
        Touch((*inp_node)->file_->path_);
}

void Toucher::TouchRule(const Rule* rule) {
  assert(rule);

  PrintHeader();
  DoTouchRule(rule);
  PrintFooter();
}

int Toucher::TouchRule(const char* rule) {
  assert(rule);

  return TouchRules(1, &rule);
}

int Toucher::TouchRules(int rule_count, const char* rules[]) {
  assert(rules);

  int status = 0;
  PrintHeader();
  for (int i = 0; i < rule_count; ++i) {
    const char* rule_name = rules[i];
    const Rule* rule = state_->LookupRule(rule_name);
    if (rule) {
      if (verbose_)
        printf("Rule %s\n", rule_name);
      DoTouchRule(rule);
    } else {
      Error("unknown rule '%s'", rule_name);
      status = 1;
    }
  }
  PrintFooter();
  return status;
}
