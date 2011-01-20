#include "ninja.h"

#include <getopt.h>
#include <limits.h>
#include <stdio.h>

#include "build.h"
#include "build_log.h"
#include "parsers.h"

#include "graphviz.h"

option options[] = {
  { "help", no_argument, NULL, 'h' },
  { }
};

void usage() {
  fprintf(stderr,
"usage: ninja [options] target\n"
"\n"
"options:\n"
"  -g       output graphviz dot file for targets and exit\n"
"  -i FILE  specify input build file [default=build.ninja]\n"
"  -n       dry run (don't run commands but pretend they succeeded)\n"
"  -v       show all command lines\n"
"  -q       show inputs/outputs of target (query mode)\n"
          );
}

struct RealFileReader : public ManifestParser::FileReader {
  bool ReadFile(const string& path, string* content, string* err) {
    return ::ReadFile(path, content, err) == 0;
  }
};

int main(int argc, char** argv) {
  BuildConfig config;
  const char* input_file = "build.ninja";
  bool graph = false;
  bool query = false;

  int opt;
  while ((opt = getopt_long(argc, argv, "ghi:nvq", options, NULL)) != -1) {
    switch (opt) {
      case 'g':
        graph = true;
        break;
      case 'i':
        input_file = optarg;
        break;
      case 'n':
        config.dry_run = true;
        break;
      case 'v':
        config.verbosity = BuildConfig::VERBOSE;
        break;
      case 'q':
        query = true;
        break;
      case 'h':
      default:
        usage();
        return 1;
    }
  }
  if (optind >= argc) {
    fprintf(stderr, "expected target to build\n");
    usage();
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

  if (graph) {
    GraphViz graph;
    graph.Start();
    for (int i = 0; i < argc; ++i)
      graph.AddTarget(state.GetNode(argv[i]));
    graph.Finish();
    return 0;
  }

  if (query) {
    for (int i = 0; i < argc; ++i) {
      Node* node = state.GetNode(argv[i]);
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
      }
    }
    return 0;
  }

  const char* kLogPath = ".ninja_log";
  if (!state.build_log_->Load(kLogPath, &err)) {
    fprintf(stderr, "error loading build log: %s\n", err.c_str());
    return 1;
  }

  if (!config.dry_run && !state.build_log_->OpenForWrite(kLogPath, &err)) {
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
