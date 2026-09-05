// Copyright 2026 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <fuzzer/FuzzedDataProvider.h>

#include "disk_interface.h"
#include "state.h"
#include "manifest_parser.h"
#include "graph.h"
#include "depfile_parser.h"
#include "explanations.h"

struct FuzzFileSystem : public DiskInterface {
  TimeStamp Stat(const std::string& path, std::string* err) const override {
    auto it = mtimes_.find(path);
    if (it != mtimes_.end()) return it->second;
    return 1;
  }
  bool WriteFile(const std::string& path, const std::string& contents, bool) override { return true; }
  bool MakeDir(const std::string& path) override { return true; }
  Status ReadFile(const std::string& path, std::string* contents, std::string* err) override {
    auto it = files_.find(path);
    if (it != files_.end()) {
      *contents = it->second;
      return Okay;
    }
    return NotFound;
  }
  int RemoveFile(const std::string& path) override { return 0; }

  std::map<std::string, TimeStamp> mtimes_;
  std::map<std::string, std::string> files_;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider fdp(data, size);

  // Split data into manifest and file system state
  std::string manifest = fdp.ConsumeRandomLengthString(size / 2);
  
  FuzzFileSystem fs;
  // Populate some random files/mtimes
  int num_files = fdp.ConsumeIntegralInRange<int>(0, 10);
  for (int i = 0; i < num_files; ++i) {
    std::string path = fdp.ConsumeRandomLengthString(20);
    if (path.empty()) continue;
    fs.mtimes_[path] = fdp.ConsumeIntegralInRange<TimeStamp>(0, 100);
    if (fdp.ConsumeBool()) {
      fs.files_[path] = fdp.ConsumeRandomLengthString(100);
    }
  }

  State state;
  ManifestParser parser(&state, &fs);
  std::string err;
  if (!parser.ParseTest(manifest, &err)) {
    return 0;
  }

  // Assign random mtimes to nodes mentioned in the manifest
  for (auto it = state.paths_.begin(); it != state.paths_.end(); ++it) {
    if (fdp.ConsumeBool()) {
        fs.mtimes_[it->first.AsString()] = fdp.ConsumeIntegralInRange<TimeStamp>(0, 100);
    }
  }

  DepfileParserOptions depfile_opts;
  Explanations explanations;
  DependencyScan scan(&state, nullptr, nullptr, &fs, &depfile_opts, &explanations);

  std::string err2;
  std::vector<Node*> roots = state.RootNodes(&err2);
  if (!err2.empty()) return 0;

  for (Node* root : roots) {
    std::string err3;
    std::vector<Node*> validation_nodes;
    scan.RecomputeDirty(root, &validation_nodes, &err3);
  }

  for (Edge* edge : state.edges_) {
    if (fdp.ConsumeBool()) {
      explanations.ExplainEdge(edge);
    }
  }

  return 0;
}
