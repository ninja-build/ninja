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

int ToolTargetsList(const vector<Node*>& nodes, int depth, int indent) {
  for (vector<Node*>::const_iterator n = nodes.begin();
       n != nodes.end();
       ++n) {
    for (int i = 0; i < indent; ++i)
      printf("  ");
    const char* target = (*n)->path().c_str();
    if ((*n)->in_edge()) {
      printf("%s: %s\n", target, (*n)->in_edge()->rule_->name().c_str());
      if (depth > 1 || depth <= 0)
        ToolTargetsList((*n)->in_edge()->inputs_, depth - 1, indent + 1);
    } else {
      printf("%s\n", target);
    }
  }
  return 0;
}

int ToolTargetsList(Execution* execution, const string& rule_name) {
  set<string> rules;

  // Gather the outputs.
  for (vector<Edge*>::const_iterator e = execution->state()->edges_.begin();
       e != execution->state()->edges_.end(); ++e) {
    if ((*e)->rule_->name() == rule_name) {
      for (vector<Node*>::iterator out_node = (*e)->outputs_.begin();
           out_node != (*e)->outputs_.end(); ++out_node) {
        rules.insert((*out_node)->path());
      }
    }
  }

  // Print them.
  for (set<string>::const_iterator i = rules.begin();
       i != rules.end(); ++i) {
    printf("%s\n", (*i).c_str());
  }

  return 0;
}

int ToolTargetsList(Execution* execution) {
  for (vector<Edge*>::const_iterator e = execution->state()->edges_.begin();
       e != execution->state()->edges_.end(); ++e) {
    for (vector<Node*>::iterator out_node = (*e)->outputs_.begin();
         out_node != (*e)->outputs_.end(); ++out_node) {
      printf("%s: %s\n",
             (*out_node)->path().c_str(),
             (*e)->rule_->name().c_str());
    }
  }
  return 0;
}

int TargetsList(const vector<Node*>& nodes, int depth, int indent) {
  for (vector<Node*>::const_iterator n = nodes.begin();
       n != nodes.end();
       ++n) {
    for (int i = 0; i < indent; ++i)
      printf("  ");
    const char* target = (*n)->path().c_str();
    if ((*n)->in_edge()) {
      printf("%s: %s\n", target, (*n)->in_edge()->rule_->name().c_str());
      if (depth > 1 || depth <= 0)
        ToolTargetsList((*n)->in_edge()->inputs_, depth - 1, indent + 1);
    } else {
      printf("%s\n", target);
    }
  }
  return 0;
}

int ToolTargetsSourceList(Execution* execution) {
  for (vector<Edge*>::const_iterator e = execution->state()->edges_.begin();
       e != execution->state()->edges_.end(); ++e) {
    for (vector<Node*>::iterator inps = (*e)->inputs_.begin();
         inps != (*e)->inputs_.end(); ++inps) {
      if (!(*inps)->in_edge())
        printf("%s\n", (*inps)->path().c_str());
    }
  }
  return 0;
}

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
  vector<Node*> nodes;
  if (argc == 0) {
    for (vector<Node*>::const_iterator ni = execution->state()->deps_log_->nodes().begin();
         ni != execution->state()->deps_log_->nodes().end(); ++ni) {
      if (execution->state()->deps_log_->IsDepsEntryLiveFor(*ni))
        nodes.push_back(*ni);
    }
  } else {
    string err;
    if (!ui::CollectTargetsFromArgs(execution->state(), argc, argv, &nodes, &err)) {
      execution->state()->Log(Logger::Level::ERROR, err);
      return 1;
    }
  }

  RealDiskInterface disk_interface;
  for (vector<Node*>::iterator it = nodes.begin(), end = nodes.end();
       it != end; ++it) {
    DepsLog::Deps* deps = execution->state()->deps_log_->GetDeps(*it);
    if (!deps) {
      printf("%s: deps not found\n", (*it)->path().c_str());
      continue;
    }

    string err;
    TimeStamp mtime = disk_interface.Stat((*it)->path(), &err);
    if (mtime == -1) {
      // Log and ignore Stat() errors;
      execution->state()->Log(Logger::Level::ERROR, err);
    }
    printf("%s: #deps %d, deps mtime %" PRId64 " (%s)\n",
           (*it)->path().c_str(), deps->node_count, deps->mtime,
           (!mtime || mtime > deps->mtime ? "STALE":"VALID"));
    for (int i = 0; i < deps->node_count; ++i)
      printf("    %s\n", deps->nodes[i]->path().c_str());
    printf("\n");
  }

  return 0;
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
  // Parse options.

  // The rules tool uses getopt, and expects argv[0] to contain the name of
  // the tool, i.e. "rules".
  argc++;
  argv--;

  bool print_description = false;

  optind = 1;
  int opt;
  while ((opt = getopt(argc, argv, const_cast<char*>("hd"))) != -1) {
    switch (opt) {
    case 'd':
      print_description = true;
      break;
    case 'h':
    default:
      printf("usage: ninja -t rules [options]\n"
             "\n"
             "options:\n"
             "  -d     also print the description of the rule\n"
             "  -h     print this message\n"
             );
    return 1;
    }
  }
  argv += optind;
  argc -= optind;

  // Print rules

  typedef map<string, const Rule*> Rules;
  const Rules& rules = execution->state()->bindings_.GetRules();
  for (Rules::const_iterator i = rules.begin(); i != rules.end(); ++i) {
    printf("%s", i->first.c_str());
    if (print_description) {
      const Rule* rule = i->second;
      const EvalString* description = rule->GetBinding("description");
      if (description != NULL) {
        printf(": %s", description->Unparse().c_str());
      }
    }
    printf("\n");
  }
  return 0;
}

int Targets(Execution* execution, int argc, char* argv[]) {
  int depth = 1;
  if (argc >= 1) {
    string mode = argv[0];
    if (mode == "rule") {
      string rule;
      if (argc > 1)
        rule = argv[1];
      if (rule.empty())
        return ToolTargetsSourceList(execution);
      else
        return ToolTargetsList(execution, rule);
    } else if (mode == "depth") {
      if (argc > 1)
        depth = atoi(argv[1]);
    } else if (mode == "all") {
      return ToolTargetsList(execution);
    } else {
      const char* suggestion =
          SpellcheckString(mode.c_str(), "rule", "depth", "all", NULL);

      ostringstream message;
      message << "unknown target tool mode '" << mode << "'";
      if (suggestion) {
        message << ", did you mean '" << suggestion << "'?";
      }
      execution->state()->Log(Logger::Level::ERROR, message.str());
      return 1;
    }
  }

  string err;
  vector<Node*> root_nodes = execution->state()->RootNodes(&err);
  if (err.empty()) {
    return ToolTargetsList(root_nodes, depth, 0);
  } else {
    execution->state()->Log(Logger::Level::ERROR, err);
    return 1;
  }
}

int Urtle(Execution* execution, int argc, char** argv) {
  // RLE encoded.
  const char* urtle =
" 13 ,3;2!2;\n8 ,;<11!;\n5 `'<10!(2`'2!\n11 ,6;, `\\. `\\9 .,c13$ec,.\n6 "
",2;11!>; `. ,;!2> .e8$2\".2 \"?7$e.\n <:<8!'` 2.3,.2` ,3!' ;,(?7\";2!2'<"
"; `?6$PF ,;,\n2 `'4!8;<!3'`2 3! ;,`'2`2'3!;4!`2.`!;2 3,2 .<!2'`).\n5 3`5"
"'2`9 `!2 `4!><3;5! J2$b,`!>;2!:2!`,d?b`!>\n26 `'-;,(<9!> $F3 )3.:!.2 d\""
"2 ) !>\n30 7`2'<3!- \"=-='5 .2 `2-=\",!>\n25 .ze9$er2 .,cd16$bc.'\n22 .e"
"14$,26$.\n21 z45$c .\n20 J50$c\n20 14$P\"`?34$b\n20 14$ dbc `2\"?22$?7$c"
"\n20 ?18$c.6 4\"8?4\" c8$P\n9 .2,.8 \"20$c.3 ._14 J9$\n .2,2c9$bec,.2 `?"
"21$c.3`4%,3%,3 c8$P\"\n22$c2 2\"?21$bc2,.2` .2,c7$P2\",cb\n23$b bc,.2\"2"
"?14$2F2\"5?2\",J5$P\" ,zd3$\n24$ ?$3?%3 `2\"2?12$bcucd3$P3\"2 2=7$\n23$P"
"\" ,3;<5!>2;,. `4\"6?2\"2 ,9;, `\"?2$\n";
  int count = 0;
  for (const char* p = urtle; *p; p++) {
    if ('0' <= *p && *p <= '9') {
      count = count*10 + *p - '0';
    } else {
      for (int i = 0; i < max(count, 1); ++i)
        printf("%c", *p);
      count = 0;
    }
  }
  return 0;
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
