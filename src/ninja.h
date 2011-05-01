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

#ifndef NINJA_NINJA_H_
#define NINJA_NINJA_H_

#include <algorithm>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include <assert.h>

using namespace std;

#include "eval_env.h"
#include "hash_map.h"
#ifdef WIN32
#include "win32port.h"
#endif
struct Edge;
struct FileStat;
struct Node;
struct Rule;

int ReadFile(const string& path, string* contents, string* err);

/// Interface for accessing the disk.
///
/// Abstract so it can be mocked out for tests.  The real implementation
/// is RealDiskInterface.
struct DiskInterface {
  virtual ~DiskInterface() {}

  /// stat() a file, returning the mtime, or 0 if missing and -1 on
  /// other errors.
  virtual int Stat(const string& path) = 0;
  /// Create a directory, returning false on failure.
  virtual bool MakeDir(const string& path) = 0;
  /// Read a file to a string.  Fill in |err| on error.
  virtual string ReadFile(const string& path, string* err) = 0;

  /// Create all the parent directories for path; like mkdir -p
  /// `basename path`.
  bool MakeDirs(const string& path);
};

/// Implementation of DiskInterface that actually hits the disk.
struct RealDiskInterface : public DiskInterface {
  virtual ~RealDiskInterface() {}
  virtual int Stat(const string& path);
  virtual bool MakeDir(const string& path);
  virtual string ReadFile(const string& path, string* err);
};

/// Mapping of path -> FileStat.
struct StatCache {
  typedef hash_map<string, FileStat*> Paths;
  Paths paths_;

  FileStat* GetFile(const string& path);

  /// Dump the mapping to stdout (useful for debugging).
  void Dump();
  void Reload();
};

/// Global state (file status, loaded rules) for a single run.
struct State {
  State();

  StatCache* stat_cache() { return &stat_cache_; }

  void AddRule(const Rule* rule);
  const Rule* LookupRule(const string& rule_name);
  Edge* AddEdge(const Rule* rule);
  Node* GetNode(const string& path);
  Node* LookupNode(const string& path);
  void AddIn(Edge* edge, const string& path);
  void AddOut(Edge* edge, const string& path);
  /// @return the root node(s) of the graph. (Root nodes have no output edges).
  /// @param error where to write the error message if somethings went wrong.
  vector<Node*> RootNodes(string* error);

  StatCache stat_cache_;
  /// All the rules used in the graph.
  map<string, const Rule*> rules_;
  /// All the edges of the graph.
  vector<Edge*> edges_;
  BindingEnv bindings_;
  struct BuildLog* build_log_;

  static const Rule kPhonyRule;
};

#endif  // NINJA_NINJA_H_
