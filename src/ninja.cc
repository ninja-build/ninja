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

#include "ninja.h"

#include <errno.h>
#ifdef WIN32
#include "getopt.h"
#else
#include <getopt.h>
#endif
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/sysctl.h>
#elif defined(linux)
#include <sys/sysinfo.h>
#endif

#include "browse.h"
#include "build.h"
#include "build_log.h"
#include "graph.h"
#include "graphviz.h"
#include "parsers.h"
#include "util.h"
#include "clean.h"
#include "touch.h"

option options[] = {
  { "help", no_argument, NULL, 'h' },
  { }
};

void usage(const BuildConfig& config) {
  fprintf(stderr,
"usage: ninja [options] target\n"
"\n"
"options:\n"
"  -f FILE  specify input build file [default=build.ninja]\n"
"  -j N     run N jobs in parallel [default=%d]\n"
"  -n       dry run (don't run commands but pretend they succeeded)\n"
"  -v       show all command lines\n"
"  -C DIR   change to DIR before doing anything else\n"
"\n"
"  -t TOOL  run a subtool.  tools are:\n"
"             browse  browse dependency graph in a web browser\n"
"             graph   output graphviz dot file for targets\n"
"             query   show inputs/outputs for a path\n"
"             targets list targets by their rule or depth in the DAG\n"
"             rules   list all rules\n"
"             clean   clean built files\n"
"             touch   touch source files\n",
          config.parallelism);
}

int GuessParallelism() {
  int processors = 0;

#if defined(linux)
  processors = get_nprocs();
#elif defined(__APPLE__) || defined(__FreeBSD__)
  size_t processors_size = sizeof(processors);
  int name[] = {CTL_HW, HW_NCPU};
  if (sysctl(name, sizeof(name) / sizeof(int),
             &processors, &processors_size,
             NULL, 0) < 0) {
    processors = 1;
  }
#elif defined(WIN32)
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  processors = info.dwNumberOfProcessors;
#endif

  switch (processors) {
  case 0:
  case 1:
    return 2;
  case 2:
    return 3;
  default:
    return processors + 2;
  }
}

struct RealFileReader : public ManifestParser::FileReader {
  bool ReadFile(const string& path, string* content, string* err) {
    return ::ReadFile(path, content, err) == 0;
  }
};

int CmdGraph(State* state, int argc, char* argv[]) {
  int status = 0;
  GraphViz graph;
  graph.Start();
  for (int i = 0; i < argc; ++i) {
    Node* node = state->LookupNode(argv[i]);
    if (node)
      graph.AddTarget(node);
    else {
      Error("unknown target '%s'", argv[i]);
      status = 1;
    }
  }
  graph.Finish();
  return status;
}

int CmdQuery(State* state, int argc, char* argv[]) {
  if (argc == 0) {
    Error("expected a target to query");
    return 1;
  }
  for (int i = 0; i < argc; ++i) {
    Node* node = state->GetNode(argv[i]);
    if (node) {
      printf("%s:\n", argv[i]);
      if (node->in_edge_) {
        printf("  input: %s\n", node->in_edge_->rule_->name_.c_str());
        for (vector<Node*>::iterator in = node->in_edge_->inputs_.begin();
             in != node->in_edge_->inputs_.end(); ++in) {
          printf("    %s\n", (*in)->file_->path_.c_str());
        }
      }
      for (vector<Edge*>::iterator edge = node->out_edges_.begin();
           edge != node->out_edges_.end(); ++edge) {
        printf("  output: %s\n", (*edge)->rule_->name_.c_str());
        for (vector<Node*>::iterator out = (*edge)->outputs_.begin();
             out != (*edge)->outputs_.end(); ++out) {
          printf("    %s\n", (*out)->file_->path_.c_str());
        }
      }
    } else {
      printf("%s unknown\n", argv[i]);
      return 1;
    }
  }
  return 0;
}

int CmdBrowse(State* state, int argc, char* argv[]) {
#ifndef WIN32
  if (argc < 1) {
    Error("expected a target to browse");
    return 1;
  }
  RunBrowsePython(state, argv[0]);
#else
  Error("browse mode not yet supported on Windows");
#endif
  // If we get here, the browse failed.
  return 1;
}

int CmdTargetsList(const vector<Node*>& nodes, int depth, int indent) {
  for (vector<Node*>::const_iterator n = nodes.begin();
       n != nodes.end();
       ++n) {
    for (int i = 0; i < indent; ++i)
      printf("  ");
    const char* target = (*n)->file_->path_.c_str();
    if ((*n)->in_edge_) {
      printf("%s: %s\n", target, (*n)->in_edge_->rule_->name_.c_str());
      if (depth > 1 || depth <= 0)
        CmdTargetsList((*n)->in_edge_->inputs_, depth - 1, indent + 1);
    } else {
      printf("%s\n", target);
    }
  }
  return 0;
}

int CmdTargetsList(const vector<Node*>& nodes, const char* rule) {
  bool found = false;
  for (vector<Node*>::const_iterator n = nodes.begin();
       n != nodes.end();
       ++n) {
    const char* target = (*n)->file_->path_.c_str();
    if ((*n)->in_edge_) {
      if (!strcmp((*n)->in_edge_->rule_->name_.c_str(), rule)) {
        printf("%s\n", target);
        found = true;
      }
      if (!CmdTargetsList((*n)->in_edge_->inputs_, rule))
        found = true;
    } else {
      if (!strncmp(rule, "", 2)) {
        printf("%s\n", target);
        found = true;
      }
    }
  }
  return (found ? 0 : 1);
}

int CmdTargetsAll(State* state) {
  if (state->edges_.empty())
    return 1;
  for (vector<Edge*>::iterator e = state->edges_.begin();
       e != state->edges_.end();
       ++e)
    for (vector<Node*>::iterator out_node = (*e)->outputs_.begin();
         out_node != (*e)->outputs_.end();
         ++out_node)
      printf("%s: %s\n",
             (*out_node)->file_->path_.c_str(),
             (*e)->rule_->name_.c_str());
  return 0;
}

int CmdTargets(State* state, int argc, char* argv[]) {
  int depth = 1;
  const char* rule = 0;
  if (argc >= 1) {
    string mode = argv[0];
    if (mode == "rule") {
      if (argc > 1)
        rule = argv[1];
      else
        rule = "";
    } else if (mode == "depth") {
      if (argc > 1)
        depth = atoi(argv[1]);
    } else if (mode == "all") {
      return CmdTargetsAll(state);
    } else {
      Error("unknown mode '%s'", mode.c_str());
      return 1;
    }
  }

  vector<Node*> root_nodes = state->RootNodes();

  if (rule)
    return CmdTargetsList(root_nodes, rule);
  else
    return CmdTargetsList(root_nodes, depth, 0);
}

int CmdRules(State* state, int argc, char* argv[]) {
  for (map<string, const Rule*>::iterator i = state->rules_.begin();
       i != state->rules_.end();
       ++i) {
    if (i->second->description_.unparsed_.empty())
      printf("%s\n", i->first.c_str());
    else
      printf("%s: %s\n",
             i->first.c_str(),
             i->second->description_.unparsed_.c_str());
  }
  return 0;
}

int CmdClean(State* state,
             int argc,
             const char* argv[],
             const BuildConfig& config) {
  Cleaner cleaner(state, config);
  if (argc >= 1)
  {
    string mode = argv[0];
    if (mode == "target") {
      if (argc >= 2) {
        return cleaner.CleanTargets(argc - 1, &argv[1]);
      } else {
        Error("expected a target to clean");
        return 1;
      }
    } else if (mode == "rule") {
      if (argc >= 2) {
        return cleaner.CleanRules(argc - 1, &argv[1]);
      } else {
        Error("expected a rule to clean");
        return 1;
      }
    } else {
      return cleaner.CleanTargets(argc, argv);
    }
  }
  else {
    cleaner.CleanAll();
    return 0;
  }
}

int CmdTouch(State* state,
             int argc,
             const char* argv[],
             const BuildConfig& config) {
  Toucher toucher(state, config);
  if (argc >= 1)
  {
    string mode = argv[0];
    if (mode == "target") {
      if (argc >= 2) {
        return toucher.TouchTargets(argc - 1, &argv[1]);
      } else {
        Error("expected at least one target to touch");
        return 1;
      }
    } else if (mode == "rule") {
      if (argc >= 2) {
        return toucher.TouchRules(argc - 1, &argv[1]);
      } else {
        Error("expected at least one rule to touch");
        return 1;
      }
    } else {
      return toucher.TouchTargets(argc, argv);
    }
  }
  else {
    toucher.TouchAll();
    return 0;
  }
}

int main(int argc, char** argv) {
  BuildConfig config;
  const char* input_file = "build.ninja";
  string tool;

  config.parallelism = GuessParallelism();

  int opt;
  while ((opt = getopt_long(argc, argv, "f:hj:nt:vC:", options, NULL)) != -1) {
    switch (opt) {
      case 'f':
        input_file = optarg;
        break;
      case 'j':
        config.parallelism = atoi(optarg);
        break;
      case 'n':
        config.dry_run = true;
        break;
      case 'v':
        config.verbosity = BuildConfig::VERBOSE;
        break;
      case 't':
        tool = optarg;
        break;
      case 'h':
      default:
        usage(config);
        return 1;
    }
  }
  if (optind >= argc && tool.empty()) {
    Error("expected target to build");
    usage(config);
    return 1;
  }
  argv += optind;
  argc -= optind;

  State state;
  RealFileReader file_reader;
  ManifestParser parser(&state, &file_reader);
  string err;
  if (!parser.Load(input_file, &err)) {
    Error("loading '%s': %s", input_file, err.c_str());
    return 1;
  }

  if (!tool.empty()) {
    if (tool == "graph")
      return CmdGraph(&state, argc, argv);
    if (tool == "query")
      return CmdQuery(&state, argc, argv);
    if (tool == "browse")
      return CmdBrowse(&state, argc, argv);
    if (tool == "targets")
      return CmdTargets(&state, argc, argv);
    if (tool == "rules")
      return CmdRules(&state, argc, argv);
    if (tool == "clean")
      return CmdClean(&state, argc, const_cast<const char**>(argv), config);
    if (tool == "touch")
      return CmdTouch(&state, argc, const_cast<const char**>(argv), config);
    Error("unknown tool '%s'", tool.c_str());
  }

  BuildLog build_log;
  build_log.SetConfig(&config);
  state.build_log_ = &build_log;

  const string build_dir = state.bindings_.LookupVariable("builddir");
  const char* kLogPath = ".ninja_log";
  string log_path = kLogPath;
  if (!build_dir.empty()) {
    if (mkdir(build_dir.c_str(), 0777) < 0 && errno != EEXIST) {
      Error("creating build directory %s: %s",
            build_dir.c_str(), strerror(errno));
      return 1;
    }
    log_path = build_dir + "/" + kLogPath;
  }

  if (!build_log.Load(log_path.c_str(), &err)) {
    Error("loading build log %s: %s",
          log_path.c_str(), err.c_str());
    return 1;
  }

  if (!build_log.OpenForWrite(log_path.c_str(), &err)) {
    Error("opening build log: %s", err.c_str());
    return 1;
  }

  Builder builder(&state, config);
  for (int i = 0; i < argc; ++i) {
    string path = argv[i];
    string err;
    if (!CanonicalizePath(&path, &err))
      Fatal("can't canonicalize '%s': %s", path.c_str(), err.c_str());

    if (!builder.AddTarget(path, &err)) {
      if (!err.empty()) {
        Error("%s", err.c_str());
        return 1;
      } else {
        // Added a target that is already up-to-date; not really
        // an error.
      }
    }
  }

  bool success = builder.Build(&err);
  if (!err.empty()) {
    printf("build stopped: %s.\n", err.c_str());
  }

  return success ? 0 : 1;
}
