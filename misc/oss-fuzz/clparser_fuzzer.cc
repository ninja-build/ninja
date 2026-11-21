// Fuzz the cl.exe /showIncludes output filter (CLParser::Parse).
//
// Threat model: when a build edge declares "deps = msvc", ninja parses the
// lines printed by the build command itself; that output is attacker-
// controlled whenever commands run from a foreign tree. Parsing depends on
// the manifest-configured "msvc_deps_prefix", so the harness derives the
// prefix from the first input line and treats the remaining lines as the
// compiler output. The prefix-match gate is attacker-influenced here, so
// plain fuzzing reaches the whole surface and no seed corpus is warranted.

#include <stdint.h>
#include <string>

#include "clparser.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::string output(reinterpret_cast<const char*>(data), size);
  size_t newline = output.find('\n');
  if (newline == std::string::npos)
    return 0;
  std::string deps_prefix = output.substr(0, newline);
  output.erase(0, newline + 1);

  CLParser parser;
  std::string filtered;
  std::string err;
  parser.Parse(output, deps_prefix, &filtered, &err);
  return 0;
}
