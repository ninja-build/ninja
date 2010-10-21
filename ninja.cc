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

  Shell shell;
  Builder builder(&state);
  Node* node = builder.AddTarget(argv[1]);
  node->in_edge_->RecomputeDirty(builder.stat_helper_);
  state.stat_cache()->Dump();

  bool success = builder.Build(&shell, &err);
  if (!err.empty()) {
    printf("%s\n", err.c_str());
  }

  return success ? 1 : 0;
}
