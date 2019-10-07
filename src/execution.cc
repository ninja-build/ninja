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

Execution::Execution() : Execution(Options()) {}

Execution::Execution(Options options) :
  options_(options),
  state_(new State()) {}

Execution::Options::Options() :
  input_file("build.ninja"),
  working_dir(NULL),
  dupe_edges_should_err(true),
  phony_cycle_should_err(false),
  depfile_distinct_target_lines_should_err(false) {}

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


}  // namespace ninja
