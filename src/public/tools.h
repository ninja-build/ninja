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
#include "public/execution.h"

namespace ninja {

class Node;
class RealDiskInterface;
class Status;

/// The type of functions that are the entry points to tools (subcommands).
typedef int (*ToolFunc)(Execution* execution, int, char**);

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
Node* SpellcheckNode(Execution* execution, const std::string& path);

/// Get the Node for a given command-line path, handling features like
/// spell correction.
Node* CollectTarget(Execution* execution, const char* cpath, std::string* err);

/// CollectTarget for all command-line arguments, filling in \a targets.
bool CollectTargetsFromArgs(Execution* execution, int argc, char* argv[],
                            std::vector<Node*>* targets, std::string* err);

/// Ensure the build directory exists, creating it if necessary.
/// @return false on error.
bool EnsureBuildDirExists(Execution* execution, RealDiskInterface* disk_interface, const BuildConfig& build_config, std::string* err);

/// Open the build log.
/// @return false on error.
bool OpenBuildLog(Execution* execution, const BuildConfig& build_config, bool recompact_only, std::string* err);

/// Open the deps log: load it, then open for writing.
/// @return false on error.
bool OpenDepsLog(Execution* execution, const BuildConfig& build_config, bool recompact_only, std::string* err);

/// Rebuild the manifest, if necessary.
/// Fills in \a err on error.
/// @return true if the manifest was rebuilt.
bool RebuildManifest(Execution* execution, const char* input_file, std::string* err, Status* status);

/// Build the targets listed on the command line.
/// @return an exit code.
int RunBuild(Execution* execution, int argc, char** argv, Status* status);

namespace tool {
int Browse(Execution* execution, int argc, char* argv[]);
int Clean(Execution* execution, int argc, char* argv[]);
int Commands(Execution* execution, int argc, char* argv[]);
int CompilationDatabase(Execution* execution, int argc, char* argv[]);
int Deps(Execution* execution, int argc, char* argv[]);
int Graph(Execution* execution, int argc, char* argv[]);
int Query(Execution* execution, int argc, char* argv[]);
int Recompact(Execution* execution, int argc, char* argv[]);
int Rules(Execution* execution, int argc, char* argv[]);
int Targets(Execution* execution, int argc, char* argv[]);
int Urtle(Execution* execution, int argc, char** argv);
#if defined(_MSC_VER)
int MSVC(Execution* execution, int argc, char* argv[]);
#endif

}  // namespace tools

}  // namespace ninja
#endif  // NINJA_PUBLIC_TOOLS_H_
