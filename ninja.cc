#include "ninja.h"

int main(int argc, char** argv) {
  State state;
  ManifestParser parser(&state);
  string err;
  if (!parser.Load("build.ninja", &err)) {
    fprintf(stderr, "error loading: %s\n", err.c_str());
    return 1;
  }
  return 0;
}
