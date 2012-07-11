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
#include "timestamp.h"

struct DiskInterface;
struct Edge;

/// Information about a node in the dependency graph: the file, whether
/// it's dirty, mtime, etc.
struct Node {
  explicit Node(const string& path)
      : path_(path),
        mtime_(-1),
        dirty_(false),
        in_edge_(NULL) {}

  /// Return true if the file exists (mtime_ got a value).
  bool Stat(DiskInterface* disk_interface);

  /// Return true if we needed to stat.
  bool StatIfNecessary(DiskInterface* disk_interface) {
    if (status_known())
      return false;
    Stat(disk_interface);
    return true;
  }

  /// Mark as not-yet-stat()ed and not dirty.
  void ResetState() {
    mtime_ = -1;
    dirty_ = false;
  }

  /// Mark the Node as already-stat()ed and missing.
  void MarkMissing() {
    mtime_ = 0;
  }

  bool exists() const {
    return mtime_ != 0;
  }

  bool status_known() const {
    return mtime_ != -1;
  }

  const string& path() const { return path_; }
  TimeStamp mtime() const { return mtime_; }

  bool dirty() const { return dirty_; }
  void set_dirty(bool dirty) { dirty_ = dirty; }
  void MarkDirty() { dirty_ = true; }

  Edge* in_edge() const { return in_edge_; }
  void set_in_edge(Edge* edge) { in_edge_ = edge; }

  const vector<Edge*>& out_edges() const { return out_edges_; }
  void AddOutEdge(Edge* edge) { out_edges_.push_back(edge); }

  void Dump(const char* prefix="") const;

private:
  string path_;
  /// Possible values of mtime_:
  ///   -1: file hasn't been examined
  ///   0:  we looked, and file doesn't exist
  ///   >0: actual file's mtime
  TimeStamp mtime_;

  /// Dirty is true when the underlying file is out-of-date.
  /// But note that Edge::outputs_ready_ is also used in judging which
  /// edges to build.
  bool dirty_;

  /// The Edge that produces this Node, or NULL when there is no
  /// known edge to produce it.
  Edge* in_edge_;

  /// All Edges that use this Node as an input.
  vector<Edge*> out_edges_;
};

/// An invokable build command and associated metadata (description, etc.).
struct Rule {
  explicit Rule(const string& name)
      : name_(name), generator_(false), restat_(false) {}

  const string& name() const { return name_; }

  bool generator() const { return generator_; }
  bool restat() const { return restat_; }

  const EvalString& command() const { return command_; }
  EvalString& command() { return command_; }
  const EvalString& description() const { return description_; }
  const EvalString& depfile() const { return depfile_; }
  const EvalString& rspfile() const { return rspfile_; }
  const EvalString& rspfile_content() const { return rspfile_content_; }

 private:
  // Allow the parsers to reach into this object and fill out its fields.
  friend struct ManifestParser;

  string name_;

  bool generator_;
  bool restat_;

  EvalString command_;
  EvalString description_;
  EvalString depfile_;
  EvalString rspfile_;
  EvalString rspfile_content_;
};

struct BuildLog;
struct Node;
struct State;

/// An edge in the dependency graph; links between Nodes using Rules.
struct Edge {
  Edge() : rule_(NULL), env_(NULL), outputs_ready_(false), implicit_deps_(0),
           order_only_deps_(0) {}

  /// Examine inputs, outputs, and command lines to judge whether this edge
  /// needs to be re-run, and update outputs_ready_ and each outputs' |dirty_|
  /// state accordingly.
  /// Returns false on failure.
  bool RecomputeDirty(State* state, DiskInterface* disk_interface, string* err);

  /// Recompute whether a given single output should be marked dirty.
  /// Returns true if so.
  bool RecomputeOutputDirty(BuildLog* build_log, TimeStamp most_recent_input,
                            Node* most_recent_node, const string& command,
                            Node* output);

  /// Return true if all inputs' in-edges are ready.
  bool AllInputsReady() const;

  /// Expand all variables in a command and return it as a string.
  /// If incl_rsp_file is enabled, the string will also contain the 
  /// full contents of a response file (if applicable)
  string EvaluateCommand(bool incl_rsp_file = false);  // XXX move to env, take env ptr
  string EvaluateDepFile();
  string GetDescription();
  
  /// Does the edge use a response file?
  bool HasRspFile();
  
  /// Get the path to the response file
  string GetRspFile();

  /// Get the contents of the response file
  string GetRspFileContent();

  bool LoadDepFile(State* state, DiskInterface* disk_interface, string* err);

  void Dump(const char* prefix="") const;

  const Rule* rule_;
  vector<Node*> inputs_;
  vector<Node*> outputs_;
  Env* env_;
  bool outputs_ready_;

  const Rule& rule() const { return *rule_; }
  bool outputs_ready() const { return outputs_ready_; }

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
