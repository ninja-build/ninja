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

#ifndef NINJA_GRAPH_H_
#define NINJA_GRAPH_H_

#include <string>
#include <vector>
using namespace std;

#include "eval_env.h"

struct DiskInterface;

struct Node;
struct FileStat {
  FileStat(const string& path) : path_(path), mtime_(-1), node_(NULL) {}

  // Return true if the file exists (mtime_ got a value).
  bool Stat(DiskInterface* disk_interface);

  // Return true if we needed to stat.
  bool StatIfNecessary(DiskInterface* disk_interface) {
    if (status_known())
      return false;
    Stat(disk_interface);
    return true;
  }

  bool exists() const {
    return mtime_ != 0;
  }

  bool status_known() const {
    return mtime_ != -1;
  }

  string path_;
  // Possible values of mtime_:
  //   -1: file hasn't been examined
  //   0:  we looked, and file doesn't exist
  //   >0: actual file's mtime
  time_t mtime_;
  Node* node_;
};

struct Edge;
struct Node {
  Node(FileStat* file) : file_(file), dirty_(false), in_edge_(NULL) {}

  bool dirty() const { return dirty_; }

  FileStat* file_;
  bool dirty_;
  Edge* in_edge_;
  vector<Edge*> out_edges_;
};

struct Rule {
  Rule(const string& name) : name_(name) { }

  bool ParseCommand(const string& command, string* err) {
    return command_.Parse(command, err);
  }
  string name_;
  EvalString command_;
  EvalString description_;
  EvalString depfile_;
};

struct State;
struct Edge {
  Edge() : rule_(NULL), env_(NULL), implicit_deps_(0), order_only_deps_(0) {}

  bool RecomputeDirty(State* state, DiskInterface* disk_interface, string* err);
  string EvaluateCommand();  // XXX move to env, take env ptr
  string GetDescription();
  bool LoadDepFile(State* state, DiskInterface* disk_interface, string* err);

  void Dump();

  const Rule* rule_;
  vector<Node*> inputs_;
  vector<Node*> outputs_;
  Env* env_;

  // XXX There are three types of inputs.
  // 1) explicit deps, which show up as $in on the command line;
  // 2) implicit deps, which the target depends on implicitly (e.g. C headers),
  //                   and changes in them cause the target to rebuild;
  // 3) order-only deps, which are needed before the target builds but which
  //                     don't cause the target to rebuild.
  // Currently we stuff all of these into inputs_ and keep counts of #2 and #3
  // when we need to compute subsets.  This is suboptimal; should think of a
  // better representation.  (Could make each pointer into a pair of a pointer
  // and a type of input, or if memory matters could use the low bits of the
  // pointer...)
  int implicit_deps_;
  int order_only_deps_;
  bool is_implicit(int index) {
    return index >= ((int)inputs_.size()) - order_only_deps_ - implicit_deps_ &&
        !is_order_only(index);
  }
  bool is_order_only(int index) {
    return index >= ((int)inputs_.size()) - order_only_deps_;
  }

  bool is_phony() const;
};

#endif  // NINJA_GRAPH_H_
