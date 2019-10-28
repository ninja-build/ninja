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

#include <stdio.h>

#include "metrics.h"
#include "state.h"

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

}  // namespace
Execution::Execution() : Execution(Options()) {}

Execution::Execution(Options options) :
  state_(new State()),
  options_(options) {
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

const BuildConfig& Execution::config() const {
  return config_;
}

const Execution::Options& Execution::options() const {
  return options_;
}

}  // namespace ninja
