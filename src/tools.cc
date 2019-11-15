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

#include "public/tools.h"

#include <iostream>
#include <sstream>

#ifdef _WIN32
#include "getopt.h"
#include <direct.h>
#include <windows.h>
#elif defined(_AIX)
#include "getopt.h"
#include <unistd.h>
#else
#include <getopt.h>
#include <unistd.h>
#endif

#include "public/ui.h"

#include "build_log.h"
#include "deps_log.h"
#include "disk_interface.h"
#include "edit_distance.h"
#include "state.h"
#include "status.h"


#include "public/execution.h"

namespace ninja {
namespace {
  static const Tool kTools[] = {
    { NULL, NULL, Tool::RUN_AFTER_FLAGS, NULL},
    // TODO(eliribble) split out build tool.
    // { "build", NULL, Tool::RUN_AFTER_FLAGS, NULL }
    { "browse", "browse dependency graph in a web browser",
      Tool::RUN_AFTER_LOAD, &tool::Browse },
    { "clean", "clean built files",
      Tool::RUN_AFTER_LOAD, &tool::Clean },
    { "commands", "list all commands required to rebuild given targets",
      Tool::RUN_AFTER_LOAD, &tool::Commands },
    { "compdb",  "dump JSON compilation database to stdout",
      Tool::RUN_AFTER_LOAD, &tool::CompilationDatabase },
    { "deps", "show dependencies stored in the deps log",
      Tool::RUN_AFTER_LOGS, &tool::Deps },
    { "graph", "output graphviz dot file for targets",
      Tool::RUN_AFTER_LOAD, &tool::Graph },
    { "list", "show available tools",
      Tool::RUN_AFTER_FLAGS, &tool::List },
    { "query", "show inputs/outputs for a path",
      Tool::RUN_AFTER_LOGS, &tool::Query },
    { "recompact",  "recompacts ninja-internal data structures",
      Tool::RUN_AFTER_LOAD, &tool::Recompact },
    { "rules",  "list all rules",
      Tool::RUN_AFTER_LOAD, &tool::Rules },
    { "targets",  "list targets by their rule or depth in the DAG",
      Tool::RUN_AFTER_LOAD, &tool::Targets },
    { "urtle", NULL,
      Tool::RUN_AFTER_FLAGS, &tool::Urtle }
#if defined(_MSC_VER)
    ,{ "msvc", "build helper for MSVC cl.exe (EXPERIMENTAL)",
      Tool::RUN_AFTER_FLAGS, &tool::MSVC }
#endif
  };
}
constexpr size_t kToolsLen = sizeof(kTools) / sizeof(kTools[0]);

namespace tool {
#if defined(NINJA_HAVE_BROWSE)
int Browse(Execution* execution, int argc, char* argv[]) {
  return execution->Browse();
}
#else
int Browse(Execution* execution, int, char**) {
  execution->state()->Log(Logger::Level::ERROR, "browse tool not supported on this platform");
  ExitNow();
  // Never reached
  return 1;
}
#endif

int Clean(Execution* execution, int argc, char* argv[]) {
  return execution->Clean();
}

int Commands(Execution* execution, int argc, char* argv[]) {
  return execution->Commands();
}

int CompilationDatabase(Execution* execution, int argc,
                                       char* argv[]) {
  return execution->CompilationDatabase();
}

int Deps(Execution* execution, int argc, char** argv) {
  return execution->Deps();
}

int Graph(Execution* execution, int argc, char* argv[]) {
  return execution->Graph();
}

int List(Execution* execution, int argc, char* argv[]) {
  execution->state()->Log(Logger::Level::INFO, "ninja subtools:\n");
  char buffer[1024];
  for (const Tool* tool = &kTools[0]; tool->name; ++tool) {
    if (tool->desc) {
      snprintf(buffer, 1024, "%10s  %s\n", tool->name, tool->desc);
      execution->state()->Log(Logger::Level::INFO, buffer);
    }
  }
  return 0;
}

int Query(Execution* execution, int argc, char* argv[]) {
  return execution->Query();
}

int Recompact(Execution* execution, int argc, char* argv[]) {
  return execution->Recompact();
}

int Rules(Execution* execution, int argc, char* argv[]) {
  return execution->Rules();
}

int Targets(Execution* execution, int argc, char* argv[]) {
  return execution->Targets();
}

int Urtle(Execution* execution, int argc, char** argv) {
  return execution->Urtle();
}

#if defined(_MSC_VER)
int MSVC(Execution* execution, int argc, char* argv[]) {
  // Reset getopt: push one argument onto the front of argv, reset optind.
  argc++;
  argv--;
  optind = 0;
  return MSVCHelperMain(argc, argv);
}
#endif

std::vector<const char*> AllNames() {
  std::vector<const char*> words;
  for (size_t i = 0; i < kToolsLen; ++i) {
    const Tool& tool = kTools[i];
    if (tool.name != NULL) {
      words.push_back(tool.name);
    }
  }
  return words;
}

const Tool* Choose(const std::string& tool_name) {
  for (size_t i = 0; i < kToolsLen; ++i) {
    const Tool& tool = kTools[i];
    if (tool.name && tool.name == tool_name)
      return &tool;
  }
  return NULL;
}

const Tool* Default() {
  return &kTools[0];
}

}  // namespace tool
}  // namespace ninja
