#include "ninja.h"

#include <getopt.h>
#include <stdio.h>

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
"  -i FILE  specify input build file [default=build.ninja]\n"
          );
}

int main(int argc, char** argv) {
  const char* input_file = "build.ninja";

  int opt;
  while ((opt = getopt_long(argc, argv, "hi:", options, NULL)) != -1) {
    switch (opt) {
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

  State state;
  ManifestParser parser(&state);
  string err;
  if (!parser.Load(input_file, &err)) {
    fprintf(stderr, "error loading '%s': %s\n", input_file, err.c_str());
    return 1;
  }
  Shell shell;
  Builder builder(&state);
  for (int i = optind; i < argc; ++i) {
    if (!builder.AddTarget(argv[i], &err)) {
      fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
  }

  bool success = builder.Build(&shell, &err);
  if (!err.empty()) {
    printf("%s\n", err.c_str());
  }

  return success ? 1 : 0;
}
