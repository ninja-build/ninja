// Fuzz the compiler depfile (.d) reader (DepfileParser::Parse).
//
// Threat model: depfiles referenced by a build.ninja "deps = gcc" edge are
// written into the source tree by build commands (compilers), so when ninja
// re-reads them their bytes are attacker-controlled in the foreign-repo
// setting. Unlike the .ninja_deps/.ninja_log readers, the depfile format has
// no magic/version gates — every byte reaches the lexer directly — so plain
// fuzzing reaches the whole file and no seed corpus is warranted.
//
// In-memory harness: DepfileParser::Parse consumes the string in place and
// requires it to be NUL-terminated (std::string guarantees the trailing NUL).

#include <stdint.h>
#include <string>

#include "depfile_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::string content(reinterpret_cast<const char*>(data), size);
  DepfileParser parser;
  std::string err;
  parser.Parse(&content, &err);
  return 0;
}
