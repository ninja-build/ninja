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
#ifndef NINJA_PUBLIC_EXECUTION_H_
#define NINJA_PUBLIC_EXECUTION_H_

#include "public/build_config.h"

namespace ninja {

class RealDiskInterface;
class State;
struct Tool;

/// Create a request to perform a ninja execution.
/// This should be the main entrypoint to requesting
/// that ninja perform some work.
class Execution {
public:
  /// Command-line options.
  struct Options {
    Options();

    /// Build file to load.
    const char* input_file;
  
    /// Directory to change into before running.
    const char* working_dir;
  
    /// Whether duplicate rules for one target should warn or print an error.
    bool dupe_edges_should_err;
  
    /// Whether phony cycles should warn or print an error.
    bool phony_cycle_should_err;
  
    /// Whether a depfile with multiple targets on separate lines should
    /// warn or print an error.
    bool depfile_distinct_target_lines_should_err;
  };

  Execution();
  Execution(Options options);

  /// Get access to the underlying disk interface
  RealDiskInterface* DiskInterface();

  /// Dump the metrics about the build requested by '-d stats'.
  void DumpMetrics();

  /// Build configuration set from flags (e.g. parallelism).
  BuildConfig config_;

  // The command used to run ninja.
  const char* ninja_command_;

  Options options_;
  State* state_;
  const Tool* tool_;
};

}  // namespace ninja

#endif // NINJA_PUBLIC_EXECUTION_H_
