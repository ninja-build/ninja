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

#include "public/execution.h"

#ifdef _WIN32
#include "getopt.h"
#elif defined(_AIX)
#include "getopt.h"
#else
#include <getopt.h>
#endif
#include <stdio.h>

#include "public/ui.h"

#include "browse.h"
#include "clean.h"
#include "debug_flags.h"
#include "deps_log.h"
#include "graphviz.h"
#include "manifest_parser.h"
#include "metrics.h"
#include "state.h"
#include "status.h"

namespace ninja {
namespace {

/// Choose a default value for the parallelism flag.
int GuessParallelism() {
  switch (int processors = GetProcessorCount()) {
  case 0:
  case 1:
    return 2;
  case 2:
    return 3;
  default:
    return processors + 2;
  }
}

Node* TargetNameToNode(const State* state, const std::string& path, std::string* err) {
  uint64_t slash_bits;
  std::string canonical_path = path.c_str();
  if (!CanonicalizePath(&canonical_path, &slash_bits, err))
    return NULL;

  // Special syntax: "foo.cc^" means "the first output of foo.cc".
  bool first_dependent = false;
  if (!canonical_path.empty() && canonical_path[canonical_path.size() - 1] == '^') {
    canonical_path.resize(canonical_path.size() - 1);
    first_dependent = true;
  }

  Node* node = state->LookupNode(canonical_path);

  if (!node) {
    *err =
        "unknown target '" + path + "'";
    if (path == "clean") {
      *err += ", did you mean 'ninja -t clean'?";
    } else if (path == "help") {
      *err += ", did you mean 'ninja -h'?";
    } else {
      Node* suggestion = ui::SpellcheckNode(state, path);
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

bool TargetNamesToNodes(const State* state, const std::vector<std::string>& names,
                            std::vector<Node*>* targets, std::string* err) {
  if (names.size() == 0) {
    *targets = state->DefaultNodes(err);
    return err->empty();
  }

  for (size_t i = 0; i < names.size(); ++i) {
    Node* node = TargetNameToNode(state, names[i], err);
    if (node == NULL)
      return false;
    targets->push_back(node);
  }
  return true;
}

}  // namespace

Execution::Execution() : Execution(NULL, Options()) {}

Execution::Execution(const char* ninja_command, Options options) :
  ninja_command_(ninja_command),
  options_(options),
  state_(new State()) {
  config_.parallelism = options_.parallelism;
  // We want to go until N jobs fail, which means we should allow
  // N failures and then stop.  For N <= 0, INT_MAX is close enough
  // to infinite for most sane builds.
  config_.failures_allowed = options_.failures_allowed;
  if (options_.depfile_distinct_target_lines_should_err) {
    config_.depfile_parser_options.depfile_distinct_target_lines_action_ =
        kDepfileDistinctTargetLinesActionError;
  }

}

Execution::Options::Options() : Options(NULL) {}
Execution::Options::Options(const Tool* tool) :
      depfile_distinct_target_lines_should_err(false),
      dry_run(false),
      dupe_edges_should_err(true),
      failures_allowed(1),
      input_file("build.ninja"),
      max_load_average(-0.0f),
      parallelism(GuessParallelism()),
      phony_cycle_should_err(false),
      tool_(tool),
      verbose(false),
      working_dir(NULL)
      {}

Execution::Options::Clean::Clean() :
  generator(false),
  targets_are_rules(false) {}

RealDiskInterface* Execution::DiskInterface() {
  return state_->disk_interface_;
}

void Execution::DumpMetrics() {
  g_metrics->Report();

  printf("\n");
  int count = (int)state_->paths_.size();
  int buckets = (int)state_->paths_.bucket_count();
  printf("path->node hash load %.2f (%d entries / %d buckets)\n",
         count / (double) buckets, count, buckets);
}

const char* Execution::command() const {
  return ninja_command_;
}

const BuildConfig& Execution::config() const {
  return config_;
}

const Execution::Options& Execution::options() const {
  return options_;
}

void Execution::LogError(const std::string& message) {
  state_->Log(Logger::Level::ERROR, message);
}
void Execution::LogWarning(const std::string& message) {
  state_->Log(Logger::Level::WARNING, message);
}

/// Rebuild the build manifest, if necessary.
/// Returns true if the manifest was rebuilt.
bool Execution::RebuildManifest(const char* input_file, string* err,
                                Status* status) {
  string path = input_file;
  uint64_t slash_bits;  // Unused because this path is only used for lookup.
  if (!CanonicalizePath(&path, &slash_bits, err))
    return false;
  Node* node = state_->LookupNode(path);
  if (!node)
    return false;

  Builder builder(state_, config_, state_->build_log_, state_->deps_log_, state_->disk_interface_,
                  status, state_->start_time_millis_);
  if (!builder.AddTarget(node, err))
    return false;

  if (builder.AlreadyUpToDate())
    return false;  // Not an error, but we didn't rebuild.

  if (!builder.Build(err))
    return false;

  // The manifest was only rebuilt if it is now dirty (it may have been cleaned
  // by a restat).
  if (!node->dirty()) {
    // Reset the state to prevent problems like
    // https://github.com/ninja-build/ninja/issues/874
    state_->Reset();
    return false;
  }

  return true;
}

int Execution::Browse() {
  const char* initial_target = NULL;
  if (options_.targets.size()) {
    if (options_.targets.size() == 1) {
      initial_target = options_.targets[0].c_str();
    } else {
      LogError("You can only specify a single target for 'browse'.");
      return 2;
    }
  }
  if(ninja_command_) {
    RunBrowsePython(ninja_command_, options_.input_file, initial_target);
  } else {
    LogError("You must specify the 'ninja_command' parameter  in your execution to browse.");
  }
  // If we get here, the browse failed.
  return 1;
}

int Execution::Clean() {
  Cleaner cleaner(state_, config_, state_->disk_interface_);
  if (options_.clean_options.targets_are_rules) {
    return cleaner.CleanRules(options_.targets);
  } else if(options_.targets.size()) {
    return cleaner.CleanTargets(options_.targets);
  } else {
    return cleaner.CleanAll(options_.clean_options.generator);
  }
}

int Execution::Graph() {
  vector<Node*> nodes;
  string err;
  if (!TargetNamesToNodes(state_, options_.targets, &nodes, &err)) {
    LogError(err);
    return 1;
  }

  GraphViz graph(state_, state_->disk_interface_);
  graph.Start();
  for (vector<Node*>::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
    graph.AddTarget(*n);
  graph.Finish();

  return 0;
}

int Execution::Query() {
  if (options_.targets.size() == 0) {
    LogError("expected a target to query");
    return 1;
  }

  DyndepLoader dyndep_loader(state_, state_->disk_interface_);

  for (size_t i = 0; i < options_.targets.size(); ++i) {
    string err;
    std::string target_name = options_.targets[i];
    Node* node = TargetNameToNode(state_, target_name, &err);
    if (!node) {
      LogError(err);
      return 1;
    }

    printf("%s:\n", node->path().c_str());
    if (Edge* edge = node->in_edge()) {
      if (edge->dyndep_ && edge->dyndep_->dyndep_pending()) {
        if (!dyndep_loader.LoadDyndeps(edge->dyndep_, &err)) {
          LogWarning(err);
        }
      }
      printf("  input: %s\n", edge->rule_->name().c_str());
      for (int in = 0; in < (int)edge->inputs_.size(); in++) {
        const char* label = "";
        if (edge->is_implicit(in))
          label = "| ";
        else if (edge->is_order_only(in))
          label = "|| ";
        printf("    %s%s\n", label, edge->inputs_[in]->path().c_str());
      }
    }
    printf("  outputs:\n");
    for (vector<Edge*>::const_iterator edge = node->out_edges().begin();
         edge != node->out_edges().end(); ++edge) {
      for (vector<Node*>::iterator out = (*edge)->outputs_.begin();
           out != (*edge)->outputs_.end(); ++out) {
        printf("    %s\n", (*out)->path().c_str());
      }
    }
  }
  return 0;
}
int Execution::Recompact() {
  string err;
  if (!EnsureBuildDirExists(&err)) {
    LogError(err);
    return 1;
  }

  if (!OpenBuildLog(true, &err) ||
      !OpenDepsLog(true, &err)) {
    LogError(err);
    return 1;
  }

  // Hack: OpenBuildLog()/OpenDepsLog() can return a warning via err
  if(!err.empty()) {
    LogWarning(err);
    err.clear();
  }

  return 0;
}

int Execution::Run(int argc, char* argv[]) {
  Status* status = new StatusPrinter(config_);

  // Limit number of rebuilds, to prevent infinite loops.
  const int kCycleLimit = 100;
  for (int cycle = 1; cycle <= kCycleLimit; ++cycle) {

    ManifestParserOptions parser_opts;
    if (options_.dupe_edges_should_err) {
      parser_opts.dupe_edge_action_ = kDupeEdgeActionError;
    }
    if (options_.phony_cycle_should_err) {
      parser_opts.phony_cycle_action_ = kPhonyCycleActionError;
    }
    ManifestParser parser(state_, DiskInterface(), parser_opts);
    string err;
    if (!parser.Load(options_.input_file, &err)) {
      status->Error("%s", err.c_str());
      return 1;
    }

    if (options_.tool_ && options_.tool_->when == Tool::RUN_AFTER_LOAD)
      return (options_.tool_->func)(this, argc, argv);

    if (!EnsureBuildDirExists(&err))
      return 1;

    if (!OpenBuildLog(false, &err) || !OpenDepsLog(false, &err)) {
      LogError(err);
      return 1;
    }

    // Hack: OpenBuildLog()/OpenDepsLog() can return a warning via err
    if(!err.empty()) {
      LogWarning(err);
      err.clear();
    }

    if (options_.tool_ && options_.tool_->when == Tool::RUN_AFTER_LOGS)
      return (options_.tool_->func)(this, argc, argv);

    // Attempt to rebuild the manifest before building anything else
    if (RebuildManifest(options_.input_file, &err, status)) {
      // In dry_run mode the regeneration will succeed without changing the
      // manifest forever. Better to return immediately.
      if (config_.dry_run)
        return 0;
      // Start the build over with the new manifest.
      continue;
    } else if (!err.empty()) {
      status->Error("rebuilding '%s': %s", options_.input_file, err.c_str());
      return 1;
    }

    int result = RunBuild(argc, argv, status);
    if (g_metrics)
      DumpMetrics();
    return result;
  }

  status->Error("manifest '%s' still dirty after %d tries",
      options_.input_file, kCycleLimit);
  return 1;
}

const State* Execution::state() const {
  return state_;
}

bool Execution::EnsureBuildDirExists(std::string* err) {
  std::string build_dir = state_->bindings_.LookupVariable("builddir");
  if (!build_dir.empty() && !config_.dry_run) {
    if (!DiskInterface()->MakeDirs(build_dir + "/.") && errno != EEXIST) {
      *err = "creating build directory " + build_dir + ": " + strerror(errno);
      return false;
    }
  }
  return true;
}

bool Execution::OpenBuildLog(bool recompact_only, std::string* err) {
  /// The build directory, used for storing the build log etc.
  std::string build_dir = state_->bindings_.LookupVariable("builddir");
  string log_path = ".ninja_log";
  if (!build_dir.empty())
    log_path = build_dir + "/" + log_path;

  if (!state_->build_log_->Load(log_path, err)) {
    *err = "loading build log " + log_path + ": " + *err;
    return false;
  }

  if (recompact_only) {
    bool success = state_->build_log_->Recompact(log_path, *state_, err);
    if (!success)
      *err = "failed recompaction: " + *err;
    return success;
  }

  if (!config_.dry_run) {
    if (!state_->build_log_->OpenForWrite(log_path, *state_, err)) {
      *err = "opening build log: " + *err;
      return false;
    }
  }

  return true;
}

/// Open the deps log: load it, then open for writing.
/// @return false on error.
bool Execution::OpenDepsLog(bool recompact_only, std::string* err) {
  std::string build_dir = state_->bindings_.LookupVariable("builddir");
  std::string path = ".ninja_deps";
  if (!build_dir.empty())
    path = build_dir + "/" + path;

  if (!state_->deps_log_->Load(path, state_, err)) {
    *err = "loading deps log " + path + ": " + *err;
    return false;
  }

  if (recompact_only) {
    bool success = state_->deps_log_->Recompact(path, err);
    if (!success)
      *err = "failed recompaction: " + *err;
    return success;
  }

  if (!config_.dry_run) {
    if (!state_->deps_log_->OpenForWrite(path, err)) {
      *err = "opening deps log: " + *err;
      return false;
    }
  }

  return true;
}

int Execution::RunBuild(int argc, char** argv, Status* status) {
  string err;
  vector<Node*> targets;
  if (!ui::CollectTargetsFromArgs(state_, argc, argv, &targets, &err)) {
    status->Error("%s", err.c_str());
    return 1;
  }

  state_->disk_interface_->AllowStatCache(g_experimental_statcache);

  Builder builder(state_, config_, state_->build_log_, state_->deps_log_, state_->disk_interface_,
                  status, state_->start_time_millis_);
  for (size_t i = 0; i < targets.size(); ++i) {
    if (!builder.AddTarget(targets[i], &err)) {
      if (!err.empty()) {
        status->Error("%s", err.c_str());
        return 1;
      } else {
        // Added a target that is already up-to-date; not really
        // an error.
      }
    }
  }

  // Make sure restat rules do not see stale timestamps.
  state_->disk_interface_->AllowStatCache(false);

  if (builder.AlreadyUpToDate()) {
    status->Info("no work to do.");
    return 0;
  }

  if (!builder.Build(&err)) {
    status->Info("build stopped: %s.", err.c_str());
    if (err.find("interrupted by user") != string::npos) {
      return 2;
    }
    return 1;
  }

  return 0;
}


}  // namespace ninja
