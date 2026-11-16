// Fuzz the dyndep loader end to end (DyndepLoader/DyndepParser + graph
// mutation).
//
// Threat model: dyndep files referenced by a build.ninja "dyndep" binding
// are written into the source tree by build commands, and ninja re-reads
// them and applies them to the build graph, so their bytes are attacker-
// controlled in the foreign-repo setting.
//
// The harness is stateful and mirrors production's sequence (ninja.cc:472-486):
// the first two bytes of the input give the length of an embedded
// build.ninja manifest, which is parsed into a State (creating the edges and
// their dyndep_ node references); the remainder is written to the dyndep
// path declared by that manifest, and DyndepLoader::LoadDyndeps parses it
// and updates the graph. The "ninja_dyndep_version" keyword and the
// requirement that dyndep outputs name outputs of real edges make the
// format mutation-resistant, so generate_dyndep_seeds.py provides valid
// manifest/dyndep pairs.
//
// Temp-file conventions match manifest_fuzzer.cc.

#include <stdint.h>
#include <stdio.h>
#include <string>

#include "disk_interface.h"
#include "dyndep.h"
#include "manifest_parser.h"
#include "state.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 2)
    return 0;
  size_t manifest_size = (static_cast<size_t>(data[0]) << 8) | data[1];
  if (manifest_size > size - 2)
    return 0;

  const char* manifest_path = "/tmp/fuzz.build.ninja";
  const char* dyndep_path = "/tmp/fuzz.dyndep.dd";

  FILE* fp = fopen(manifest_path, "wb");
  if (!fp)
    return 0;
  fwrite(data + 2, 1, manifest_size, fp);
  fclose(fp);

  std::string err;
  RealDiskInterface disk_interface;
  State state;
  ManifestParser parser(&state, &disk_interface);
  // Parse errors are expected for arbitrary bytes; proceed either way.
  parser.Load(manifest_path, &err);

  size_t dyndep_size = size - 2 - manifest_size;
  fp = fopen(dyndep_path, "wb");
  if (!fp) {
    remove(manifest_path);
    return 0;
  }
  fwrite(data + 2 + manifest_size, 1, dyndep_size, fp);
  fclose(fp);

  // Production loads the dyndep file through the node the manifest's dyndep
  // bindings created; find it the same way ninja.cc does.
  for (Edge* edge : state.edges_) {
    if (edge->dyndep_ && edge->dyndep_->dyndep_pending()) {
      DyndepLoader loader(&state, &disk_interface);
      DyndepFile ddf;
      loader.LoadDyndeps(edge->dyndep_, &ddf, &err);
    }
  }

  remove(manifest_path);
  remove(dyndep_path);
  return 0;
}
