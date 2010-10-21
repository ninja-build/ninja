#include "ninja.h"

int main(int argc, char** argv) {
  State state;
  ManifestParser parser(&state);
  string err;
  if (!parser.Load("build.ninja", &err)) {
    fprintf(stderr, "error loading: %s\n", err.c_str());
    return 1;
  }

  if (argc < 2) {
    fprintf(stderr, "usage: %s target\n", argv[0]);
    return 1;
  }

  state.stat_cache()->Dump();
  Shell shell;
  Builder builder(&state);
  builder.AddTarget(argv[1]);
  state.stat_cache()->Dump();

  bool success = builder.Build(&shell, &err);
  if (!err.empty()) {
    printf("%s\n", err.c_str());
  }

  return success ? 1 : 0;
}
