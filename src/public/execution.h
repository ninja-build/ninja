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
#include "public/tools.h"

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
    Options(const Tool* tool);

    /// Whether a depfile with multiple targets on separate lines should
    /// warn or print an error.
    bool depfile_distinct_target_lines_should_err;

    /// Whether or not this is a dry-run. IE, it should just
    /// show what would be performed without taking any action.
    bool dry_run;

    /// Whether duplicate rules for one target should warn or print an error.
    bool dupe_edges_should_err;
 
    /// The number of failures allowed before terminating the build.
    int failures_allowed;

    /// Build file to load.
    const char* input_file;
 
    /// The maximum load to allow.
    float max_load_average;

    /// The level of parallelism to use during the build
    int parallelism;
 
    /// Whether phony cycles should warn or print an error.
    bool phony_cycle_should_err;
  
    /// The tool to use
    const Tool* tool_;

    /// True to include verbose logging. Default is false.
    bool verbose;

    /// Directory to change into before running.
    const char* working_dir;
  
  };

  /// Default constructor. This should be used primarily
  /// by tests as the defaults it provides are quite poor.
  Execution();

  /// Construct a new ninja execution. The first parameter,
  /// ninja_command should be a string that could be provided
  /// to the operating system in order to run ninja itself.
  /// This is used for some sub-commands of ninja that need to
  /// start a new ninja subprocess.
  Execution(const char* ninja_command, Options options);

  /// Get access to the underlying disk interface
  RealDiskInterface* DiskInterface();

  /// Dump the metrics about the build requested by '-d stats'.
  void DumpMetrics();

  /// Get read-only access to command used to start this
  /// ninja execution.
  const char* command() const;

  /// Get read-only access to underlying build config
  const BuildConfig& config() const;

  /// Get read-only access to the underlying options
  const Options& options() const;

  State* state_;

private:
  // The command used to run ninja.
  const char* ninja_command_;

  /// Build configuration set from flags (e.g. parallelism).
  BuildConfig config_;

  /// The options provided to this execution when it is built
  /// that control most of the execution's behavior.
  Options options_;
};

}  // namespace ninja

#endif // NINJA_PUBLIC_EXECUTION_H_
