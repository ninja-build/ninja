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

class Execution;
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

namespace tool {
// Get the names of all valid tools
std::vector<const char*> AllNames();

// Get a tool reference by name
const Tool* Choose(const std::string& name);

// Get the default tool that ninja uses to build
const Tool* Default();

int Browse(Execution* execution, int argc, char* argv[]);
int Clean(Execution* execution, int argc, char* argv[]);
int Commands(Execution* execution, int argc, char* argv[]);
int CompilationDatabase(Execution* execution, int argc, char* argv[]);
int Deps(Execution* execution, int argc, char* argv[]);
int Graph(Execution* execution, int argc, char* argv[]);
int List(Execution* execution, int argc, char* argv[]);
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
