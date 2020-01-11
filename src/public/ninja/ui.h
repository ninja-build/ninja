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

#ifndef NINJA_PUBLIC_UI_H_
#define NINJA_PUBLIC_UI_H_

#include "ninja/build_config.h"
#include "ninja/execution.h"

#ifdef _WIN32
#include "ninja/win32port.h"
#else
#include <stdint.h>
#endif

#ifdef _MSC_VER
#define NORETURN __declspec(noreturn)
#else
#define NORETURN __attribute__((noreturn))
#endif

#include <vector>

namespace ninja {

class Node;
class State;

namespace ui {

const char* Error();
const char* Info();
const char* Warning();

/// Subtools, accessible via "-t foo".
struct Tool {
  /// Short name of the tool.
  const char* name;

  /// Description (shown in "-t list").
  const char* desc;

  typedef int (Execution::*Implementation) (void);
  /// The implementation of the tool
  Implementation implementation;
};

/// Structure for returning the information parsed
/// from the commandline arguments.
struct ParsedFlags {
  const Tool* tool = NULL;
  Execution::Options options;
};

// Get the names of all valid tools
std::vector<const char*> AllToolNames();

// Get a tool reference by name
const Tool* ChooseTool(const std::string& name);

/// Get the Node for a given command-line path, handling features like
/// spell correction.
Node* CollectTarget(const State* state, const char* cpath, std::string* err);

/// CollectTarget for all command-line arguments, filling in \a targets.
bool CollectTargetsFromArgs(const State* state, int argc, char* argv[],
                            std::vector<Node*>* targets, std::string* err);

// Get the default tool that ninja uses to build
const Tool* DefaultTool();

/// Execute ninja as the main ninja binary would
/// Does not return, prefering to exit() directly
/// to avoid potentially expensive cleanup when  destructuring
/// Ninja's state.
NORETURN void Execute(int argc, char** argv);

// Exit the program immediately with a nonzero status
void ExitNow();

/// List the tools available to ninja
void ListTools();

/// Parse argv for command-line options.
/// Returns an exit code, or -1 if Ninja should continue.
int ReadFlags(int* argc, char*** argv, ParsedFlags* flags);

/// Parse argv for 'clean' tool.
/// Returns an exit code, or -1 if Ninja should continue.
int ReadFlagsClean(int* argc, char*** argv, Execution::Options* options);

/// Parse argv for 'commands' tool.
/// Returns an exit code, or -1 if Ninja should continue.
int ReadFlagsCommands(int* argc, char*** argv, Execution::Options* options);

/// Parse argv for 'compdb' tool.
/// Returns an exit code, or -1 if Ninja should continue.
int ReadFlagsCompilationDatabase(int* argc, char*** argv, Execution::Options* options);

/// Parse argv for 'msvc' tool.
/// Returns an exit code, or -1 if Ninja should continue.
int ReadFlagsMSVC(int* argc, char*** argv, Execution::Options* options);

/// Parse argv for 'rules' tool.
/// Returns an exit code or -1 if Ninja should continue.
int ReadFlagsRules(int* argc, char*** argv, Execution::Options* options);

/// Parse argv for 'targets' tool.
/// Returns an exit code or -1 if Ninja should continue.
int ReadFlagsTargets(int* argc, char*** argv, Execution::Options* options);

/// Parse targets from argv. Targets are used by several different tools.
/// Returns an exit code, or -1 if Ninja should continue.
int ReadTargets(int* argc, char*** argv, Execution::Options* options);

// Get a suggested tool name given a name that is supposed
// to be like a tool.
const char* GetToolNameSuggestion(const std::string& tool_name);

// Given a path to a node return the node that is
// most likely a match for that path. This accounts
// for users mispelling parts of the path and gives
// the node nearest what the user requested.
Node* SpellcheckNode(const State* state, const std::string& path);

/// Print usage information.
void Usage(const Execution::Options* options);

}  // namespace ui
}  // namespace ninja
#endif  // NINJA_PUBLIC_UI_H_
