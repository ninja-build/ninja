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
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/sysctl.h>
#elif defined(linux)
#include <sys/sysinfo.h>
#endif

#ifdef WIN32
#include "getopt.h"
#include <direct.h>
#include <windows.h>
#else
#include <getopt.h>
#endif

#include "browse.h"
#include "build.h"
#include "build_log.h"
#include "graph.h"
#include "graphviz.h"
#include "parsers.h"
#include "util.h"
#include "clean.h"

namespace {

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
"  -k N     keep going until N jobs fail [default=1]\n"
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
"             clean   clean built files\n",
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

/// An implementation of ManifestParser::FileReader that actually reads
/// the file.
struct RealFileReader : public ManifestParser::FileReader {
  bool ReadFile(const string& path, string* content, string* err) {
    return ::ReadFile(path, content, err) == 0;
  }
};

bool CollectTargetsFromArgs(State* state, int argc, char* argv[],
                            vector<Node*>* targets, string* err) {
  if (argc == 0) {
    *targets = state->DefaultNodes(err);
    if (!err->empty())
      return false;
  } else {
    for (int i = 0; i < argc; ++i) {
      string path = argv[i];
      CanonicalizePath(&path);
      Node* node = state->LookupNode(path);
      if (node) {
        targets->push_back(node);
      } else {
        *err = "unknown target '" + path + "'";
        return false;
      }
    }
  }
  return true;
}

int CmdGraph(State* state, int argc, char* argv[]) {
  vector<Node*> nodes;
  string err;
  if (!CollectTargetsFromArgs(state, argc, argv, &nodes, &err)) {
    Error("%s", err.c_str());
    return 1;
  }

  GraphViz graph;
  graph.Start();
  for (vector<Node*>::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
    graph.AddTarget(*n);
  graph.Finish();

  return 0;
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

int CmdBrowse(State* state, const char* ninja_command,
              int argc, char* argv[]) {
#ifndef WIN32
  if (argc < 1) {
    Error("expected a target to browse");
    return 1;
  }
  RunBrowsePython(state, ninja_command, argv[0]);
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

int CmdTargetsList(const vector<Node*>& nodes, int depth) {
  return CmdTargetsList(nodes, depth, 0);
}

int CmdTargetsSourceList(State* state) {
  for (vector<Edge*>::iterator e = state->edges_.begin();
       e != state->edges_.end();
       ++e)
    for (vector<Node*>::iterator inps = (*e)->inputs_.begin();
         inps != (*e)->inputs_.end();
         ++inps)
      if (!(*inps)->in_edge_)
        printf("%s\n", (*inps)->file_->path_.c_str());
  return 0;
}

int CmdTargetsList(State* state, const string& rule_name) {
  set<string> rules;
  // Gather the outputs.
  for (vector<Edge*>::iterator e = state->edges_.begin();
       e != state->edges_.end();
       ++e)
    if ((*e)->rule_->name_ == rule_name)
      for (vector<Node*>::iterator out_node = (*e)->outputs_.begin();
           out_node != (*e)->outputs_.end();
           ++out_node)
        rules.insert((*out_node)->file_->path_);
  // Print them.
  for (set<string>::const_iterator i = rules.begin();
       i != rules.end();
       ++i)
    printf("%s\n", (*i).c_str());
  return 0;
}

int CmdTargetsList(State* state) {
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
  if (argc >= 1) {
    string mode = argv[0];
    if (mode == "rule") {
      string rule;
      if (argc > 1)
        rule = argv[1];
      if (rule.empty())
        return CmdTargetsSourceList(state);
      else
        return CmdTargetsList(state, rule);
    } else if (mode == "depth") {
      if (argc > 1)
        depth = atoi(argv[1]);
    } else if (mode == "all") {
      return CmdTargetsList(state);
    } else {
      Error("unknown target tool mode '%s'", mode.c_str());
      return 1;
    }
  }

  string err;
  vector<Node*> root_nodes = state->RootNodes(&err);
  if (err.empty()) {
    return CmdTargetsList(root_nodes, depth);
  } else {
    Error("%s", err.c_str());
    return 1;
  }
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
             char* argv[],
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
    return cleaner.CleanAll();
  }
}

}  // anonymous namespace

int main(int argc, char** argv) {
  const char* ninja_command = argv[0];
  BuildConfig config;
  const char* input_file = "build.ninja";
  const char* working_dir = 0;
  string tool;

  setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

  config.parallelism = GuessParallelism();

  int opt;
  while ((opt = getopt_long(argc, argv, "f:hj:k:nt:vC:", options,
                            NULL)) != -1) {
    switch (opt) {
      case 'f':
        input_file = optarg;
        break;
      case 'j':
        config.parallelism = atoi(optarg);
        break;
      case 'k':
        // We want to go until N jobs fail, which means we should ignore
        // the first N-1 that fail and then stop.
        config.swallow_failures = atoi(optarg) - 1;
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
      case 'C':
        working_dir = optarg;
        break;
      case 'h':
      default:
        usage(config);
        return 1;
    }
  }
  argv += optind;
  argc -= optind;

  if (working_dir) {
#ifdef _WIN32
    if (_chdir(working_dir) < 0) {
#else
    if (chdir(working_dir) < 0) {
#endif
      Fatal("chdir to '%s' - %s", working_dir, strerror(errno));
    }
  }

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
      return CmdBrowse(&state, ninja_command, argc, argv);
    if (tool == "targets")
      return CmdTargets(&state, argc, argv);
    if (tool == "rules")
      return CmdRules(&state, argc, argv);
    if (tool == "clean")
      return CmdClean(&state, argc, argv, config);
    Error("unknown tool '%s'", tool.c_str());
  }

  BuildLog build_log;
  build_log.SetConfig(&config);
  state.build_log_ = &build_log;

  const string build_dir = state.bindings_.LookupVariable("builddir");
  const char* kLogPath = ".ninja_log";
  string log_path = kLogPath;
  if (!build_dir.empty()) {
    if (MakeDir(build_dir) < 0 && errno != EEXIST) {
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

  vector<Node*> targets;
  if (!CollectTargetsFromArgs(&state, argc, argv, &targets, &err)) {
    Error("%s", err.c_str());
    return 1;
  }

  Builder builder(&state, config);
  for (size_t i = 0; i < targets.size(); ++i) {
    if (!builder.AddTarget(targets[i], &err)) {
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
