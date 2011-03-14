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
#include <limits.h>
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
"\n"
"  -t TOOL  run a subtool.  tools are:\n"
"             browse  browse dependency graph in a web browser\n"
"             graph   output graphviz dot file for targets\n"
"             query   show inputs/outputs for a path\n",
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
    processors = 2; break;
  case 2:
    processors = 3; break;
  default:
    processors = processors + 2; break;
  }

  return processors;
}

struct RealFileReader : public ManifestParser::FileReader {
  bool ReadFile(const string& path, string* content, string* err) {
    return ::ReadFile(path, content, err) == 0;
  }
};

int CmdGraph(State* state, int argc, char* argv[]) {
  GraphViz graph;
  graph.Start();
  for (int i = 0; i < argc; ++i)
    graph.AddTarget(state->GetNode(argv[i]));
  graph.Finish();
  return 0;
}

int CmdQuery(State* state, int argc, char* argv[]) {
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
  RunBrowsePython(state, argv[0]);
#else
  printf("ERROR: Not supported on win32 platform, aborting.\n");
#endif
  // If we get here, the browse failed.
  return 1;
}

int main(int argc, char** argv) {
  BuildConfig config;
  const char* input_file = "build.ninja";
  string tool;

  config.parallelism = GuessParallelism();

  int opt;
  while ((opt = getopt_long(argc, argv, "f:hj:nt:v", options, NULL)) != -1) {
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
  if (optind >= argc) {
    fprintf(stderr, "expected target to build\n");
    usage(config);
    return 1;
  }
  argv += optind;
  argc -= optind;

  char cwd[PATH_MAX];
  if (!getcwd(cwd, sizeof(cwd))) {
    perror("getcwd");
    return 1;
  }

  State state;
  RealFileReader file_reader;
  ManifestParser parser(&state, &file_reader);
  string err;
  if (!parser.Load(input_file, &err)) {
    fprintf(stderr, "error loading '%s': %s\n", input_file, err.c_str());
    return 1;
  }

  if (!tool.empty()) {
    if (tool == "graph")
      return CmdGraph(&state, argc, argv);
    if (tool == "query")
      return CmdQuery(&state, argc, argv);
    if (tool == "browse")
      return CmdBrowse(&state, argc, argv);
    fprintf(stderr, "unknown tool '%s'\n", tool.c_str());
  }

  BuildLog build_log;
  build_log.SetConfig(&config);
  state.build_log_ = &build_log;

  const string build_dir = state.bindings_.LookupVariable("builddir");
  const char* kLogPath = ".ninja_log";
  string log_path = kLogPath;
  if (!build_dir.empty()) {
    if (mkdir(build_dir.c_str(), 0777) < 0 && errno != EEXIST) {
      fprintf(stderr, "Error creating build directory %s: %s\n",
              build_dir.c_str(), strerror(errno));
      return 1;
    }
    log_path = build_dir + "/" + kLogPath;
  }

  if (!build_log.Load(log_path.c_str(), &err)) {
    fprintf(stderr, "error loading build log %s: %s\n",
            log_path.c_str(), err.c_str());
    return 1;
  }

  if (!build_log.OpenForWrite(log_path.c_str(), &err)) {
    fprintf(stderr, "error opening build log: %s\n", err.c_str());
    return 1;
  }

  Builder builder(&state, config);
  for (int i = 0; i < argc; ++i) {
    if (!builder.AddTarget(argv[i], &err)) {
      if (!err.empty()) {
        fprintf(stderr, "%s\n", err.c_str());
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
