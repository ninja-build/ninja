// Copyright 2019 Google Inc. All Rights Reserved.
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

#include "public/tools.h"

#include "build_log.h"
#include "deps_log.h"
#include "disk_interface.h"
#include "edit_distance.h"
#include "state.h"


namespace ninja {

Node* SpellcheckNode(State* state, const std::string& path) {
  const bool kAllowReplacements = true;
  const int kMaxValidEditDistance = 3;

  int min_distance = kMaxValidEditDistance + 1;
  Node* result = NULL;
  for (State::Paths::iterator i = state->paths_.begin(); i != state->paths_.end(); ++i) {
    int distance = EditDistance(
        i->first, path, kAllowReplacements, kMaxValidEditDistance);
    if (distance < min_distance && i->second) {
      min_distance = distance;
      result = i->second;
    }
  }
  return result;
}

Node* CollectTarget(State* state, const char* cpath, std::string* err) {
  std::string path = cpath;
  uint64_t slash_bits;
  if (!CanonicalizePath(&path, &slash_bits, err))
    return NULL;

  // Special syntax: "foo.cc^" means "the first output of foo.cc".
  bool first_dependent = false;
  if (!path.empty() && path[path.size() - 1] == '^') {
    path.resize(path.size() - 1);
    first_dependent = true;
  }

  Node* node = state->LookupNode(path);

  if (!node) {
    *err =
        "unknown target '" + Node::PathDecanonicalized(path, slash_bits) + "'";
    if (path == "clean") {
      *err += ", did you mean 'ninja -t clean'?";
    } else if (path == "help") {
      *err += ", did you mean 'ninja -h'?";
    } else {
      Node* suggestion = SpellcheckNode(state, path);
      if (suggestion) {
        *err += ", did you mean '" + suggestion->path() + "'?";
      }
    }
    return NULL;
  }

  if (!first_dependent) {
    return node;
  }

  if (node->out_edges().empty()) {
    *err = "'" + path + "' has no out edge";
    return NULL;
  }

  Edge* edge = node->out_edges()[0];
  if (edge->outputs_.empty()) {
    edge->Dump();
    *err = "edge has no outputs";
    return NULL;
  }
  return edge->outputs_[0];
}

bool CollectTargetsFromArgs(State* state, int argc, char* argv[],
                            std::vector<Node*>* targets, std::string* err) {
  if (argc == 0) {
    *targets = state->DefaultNodes(err);
    return err->empty();
  }

  for (int i = 0; i < argc; ++i) {
    Node* node = CollectTarget(state, argv[i], err);
    if (node == NULL)
      return false;
    targets->push_back(node);
  }
  return true;
}

bool EnsureBuildDirExists(State* state, RealDiskInterface* disk_interface, const BuildConfig& build_config, std::string* err) {
  std::string build_dir = state->bindings_.LookupVariable("builddir");
  if (!build_dir.empty() && !build_config.dry_run) {
    if (!disk_interface->MakeDirs(build_dir + "/.") && errno != EEXIST) {
      *err = "creating build directory " + build_dir + ": " + strerror(errno);
      return false;
    }
  }
  return true;
}

bool OpenBuildLog(State* state, const BuildConfig& build_config, const BuildLogUser& user, bool recompact_only, std::string* err) {
  /// The build directory, used for storing the build log etc.
  std::string build_dir = state->bindings_.LookupVariable("builddir");
  string log_path = ".ninja_log";
  if (!build_dir.empty())
    log_path = build_dir + "/" + log_path;

  if (!state->build_log_->Load(log_path, err)) {
    *err = "loading build log " + log_path + ": " + *err;
    return false;
  }

  if (recompact_only) {
    bool success = state->build_log_->Recompact(log_path, user, err);
    if (!success)
      *err = "failed recompaction: " + *err;
    return success;
  }

  if (!build_config.dry_run) {
    if (!state->build_log_->OpenForWrite(log_path, user, err)) {
      *err = "opening build log: " + *err;
      return false;
    }
  }

  return true;
}

/// Open the deps log: load it, then open for writing.
/// @return false on error.
bool OpenDepsLog(State* state, const BuildConfig& build_config, bool recompact_only, std::string* err) {
  std::string build_dir = state->bindings_.LookupVariable("builddir");
  std::string path = ".ninja_deps";
  if (!build_dir.empty())
    path = build_dir + "/" + path;

  if (!state->deps_log_->Load(path, state, err)) {
    *err = "loading deps log " + path + ": " + *err;
    return false;
  }

  if (recompact_only) {
    bool success = state->deps_log_->Recompact(path, err);
    if (!success)
      *err = "failed recompaction: " + *err;
    return success;
  }

  if (!build_config.dry_run) {
    if (!state->deps_log_->OpenForWrite(path, err)) {
      *err = "opening deps log: " + *err;
      return false;
    }
  }

  return true;
}

}  // namespace ninja
