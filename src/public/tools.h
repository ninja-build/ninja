// Copyright 2019 Google Inc. All Rights Reserved.
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
#ifndef NINJA_PUBLIC_TOOLS_H_
#define NINJA_PUBLIC_TOOLS_H_

#include <string>
#include <vector>

#include "public/build_config.h"

namespace ninja {

class Node;
class RealDiskInterface;
class State;
struct Tool;

/// Command-line options.
struct Options {
  /// Build file to load.
  const char* input_file;

  /// Directory to change into before running.
  const char* working_dir;

  /// Tool to run rather than building.
  const Tool* tool;

  /// Whether duplicate rules for one target should warn or print an error.
  bool dupe_edges_should_err;

  /// Whether phony cycles should warn or print an error.
  bool phony_cycle_should_err;

  /// Whether a depfile with multiple targets on separate lines should
  /// warn or print an error.
  bool depfile_distinct_target_lines_should_err;
};

/// The type of functions that are the entry points to tools (subcommands).
typedef int (*ToolFunc)(State* state_, const Options*, int, char**);

/// Subtools, accessible via "-t foo".
struct Tool {
  /// Short name of the tool.
  const char* name;

  /// Description (shown in "-t list").
  const char* desc;

  /// When to run the tool.
  enum {
    /// Run after parsing the command-line flags and potentially changing
    /// the current working directory (as early as possible).
    RUN_AFTER_FLAGS,

    /// Run after loading build.ninja.
    RUN_AFTER_LOAD,

    /// Run after loading the build/deps logs.
    RUN_AFTER_LOGS,
  } when;

  /// Implementation of the tool.
  ToolFunc func;
};

// Given a path to a node return the node that is
// most likely a match for that path. This accounts
// for users mispelling parts of the path and gives
// the node nearest what the user requested.
Node* SpellcheckNode(State* state, const std::string& path);

/// Get the Node for a given command-line path, handling features like
/// spell correction.
Node* CollectTarget(State* state, const char* cpath, std::string* err);

/// CollectTarget for all command-line arguments, filling in \a targets.
bool CollectTargetsFromArgs(State* state, int argc, char* argv[],
                            std::vector<Node*>* targets, std::string* err);

/// Ensure the build directory exists, creating it if necessary.
/// @return false on error.
bool EnsureBuildDirExists(State* state, RealDiskInterface* disk_interface, const BuildConfig& build_config, std::string* err);

/// Open the build log.
/// @return false on error.
bool OpenBuildLog(State* state, const BuildConfig& build_config, bool recompact_only, std::string* err);

/// Open the deps log: load it, then open for writing.
/// @return false on error.
bool OpenDepsLog(State* state, const BuildConfig& build_config, bool recompact_only, std::string* err);

}  // namespace ninja
#endif  // NINJA_PUBLIC_TOOLS_H_
