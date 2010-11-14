#include "ninja.h"

#include <getopt.h>
#include <limits.h>
#include <stdio.h>

#include "graphviz.h"
#include "parsers.h"

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
          );
}

struct RealFileReader : public ManifestParser::FileReader {
  bool ReadFile(const string& path, string* content, string* err) {
    return ::ReadFile(path, content, err) == 0;
  }
};

int main(int argc, char** argv) {
  const char* input_file = "build.ninja";

  int opt;
  bool graph = false;
  while ((opt = getopt_long(argc, argv, "ghi:", options, NULL)) != -1) {
    switch (opt) {
      case 'g':
        graph = true;
        break;
      case 'i':
        input_file = optarg;
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
  parser.set_root(cwd);
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

  Shell shell;
  Builder builder(&state);
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

  bool success = builder.Build(&shell, &err);
  if (!err.empty()) {
    printf("%s\n", err.c_str());
  }

  return success ? 0 : 1;
}
