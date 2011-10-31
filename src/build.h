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

#ifndef NINJA_BUILD_H_
#define NINJA_BUILD_H_

#include <map>
#include <set>
#include <string>
#include <queue>
#include <vector>
using namespace std;

struct BuildLog;
struct Edge;
struct DiskInterface;
struct Node;
struct State;

/// Plan stores the state of a build plan: what we intend to build,
/// which steps we're ready to execute.
struct Plan {
  Plan();

  /// Add a target to our plan (including all its dependencies).
  /// Returns false if we don't need to build this target; may
  /// fill in |err| with an error message if there's a problem.
  bool AddTarget(Node* node, string* err);

  // Pop a ready edge off the queue of edges to build.
  // Returns NULL if there's no work to do.
  Edge* FindWork();

  /// Returns true if there's more work to be done.
  bool more_to_do() const { return wanted_edges_; }

  /// Dumps the current state of the plan.
  void Dump();

  /// Mark an edge as done building.  Used internally and by
  /// tests.
  void EdgeFinished(Edge* edge);

  /// Clean the given node during the build.
  void CleanNode(BuildLog* build_log, Node* node);

  /// Number of edges with commands to run.
  int command_edge_count() const { return command_edges_; }

private:
  bool AddSubTarget(Node* node, vector<Node*>* stack, string* err);
  bool CheckDependencyCycle(Node* node, vector<Node*>* stack, string* err);
  void NodeFinished(Node* node);

  /// Keep track of which edges we want to build in this plan.  If this map does
  /// not contain an entry for an edge, we do not want to build the entry or its
  /// dependents.  If an entry maps to false, we do not want to build it, but we
  /// might want to build one of its dependents.  If the entry maps to true, we
  /// want to build it.
  map<Edge*, bool> want_;

  set<Edge*> ready_;

  /// Total number of edges that have commands (not phony).
  int command_edges_;

  /// Total remaining number of wanted edges.
  int wanted_edges_;
};

/// CommandRunner is an interface that wraps running the build
/// subcommands.  This allows tests to abstract out running commands.
/// RealCommandRunner is an implementation that actually runs commands.
struct CommandRunner {
  virtual ~CommandRunner() {}
  virtual bool CanRunMore() = 0;
  virtual bool StartCommand(Edge* edge) = 0;
  /// Wait for a command to complete.
  virtual Edge* WaitForCommand(bool* success, string* output) = 0;
};

/// Options (e.g. verbosity, parallelism) passed to a build.
struct BuildConfig {
  BuildConfig() : verbosity(NORMAL), dry_run(false), parallelism(1),
                  swallow_failures(0) {}

  enum Verbosity {
    NORMAL,
    QUIET,  // No output -- used when testing.
    VERBOSE
  };
  Verbosity verbosity;
  bool dry_run;
  int parallelism;
  int swallow_failures;
};

/// Builder wraps the build process: starting commands, updating status.
struct Builder {
  Builder(State* state, const BuildConfig& config);

  Node* AddTarget(const string& name, string* err);

  /// Add a target to the build, scanning dependencies.
  /// @return false on error.
  bool AddTarget(Node* target, string* err);

  /// Returns true if the build targets are already up to date.
  bool AlreadyUpToDate() const;

  /// Run the build.  Returns false on error.
  /// It is an error to call this function when AlreadyUpToDate() is true.
  bool Build(string* err);

  bool StartEdge(Edge* edge, string* err);
  void FinishEdge(Edge* edge, bool success, const string& output);

  State* state_;
  const BuildConfig& config_;
  Plan plan_;
  DiskInterface* disk_interface_;
  CommandRunner* command_runner_;
  struct BuildStatus* status_;
  struct BuildLog* log_;
};

#endif  // NINJA_BUILD_H_
