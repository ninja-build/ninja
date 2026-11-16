// Fuzz the .ninja_log reader (BuildLog::Load).
//
// Threat model: a .ninja_log in a foreign source tree is parsed by ninja at
// startup before any build command runs, so its bytes are attacker-controlled
// in that setting. Parsing is gated behind the magic first line
// "# ninja log v%d" which must carry version 7 (build_log.cc:53-55,
// :226-235) plus five tab-separated fields per entry, which plain mutation
// cannot cross: misc/oss-fuzz/generate_build_log_seeds.py constructs
// structurally valid logs instead.
//
// Temp-file convention matches manifest_fuzzer.cc: BuildLog::Load only takes
// a path. Entry lifetimes are unique_ptr-owned (build_log.cc:289) so a
// per-iteration BuildLog does not leak.

#include <stdint.h>
#include <stdio.h>
#include <string>

#include "build_log.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const char* path = "/tmp/fuzz.ninja_log";
  FILE* fp = fopen(path, "wb");
  if (!fp)
    return 0;
  fwrite(data, 1, size, fp);
  fclose(fp);

  std::string err;
  BuildLog build_log;
  build_log.Load(path, &err);

  remove(path);
  return 0;
}
