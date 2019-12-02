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

#include <memory>
#include <string>
#include <vector>

#include "public/build_config.h"
#include "public/logger.h"

namespace ninja {

class RealDiskInterface;
class State;
struct Status;

/// Create a request to perform a ninja execution.
/// This should be the main entrypoint to requesting
/// that ninja perform some work.
class Execution {
public:
  /// Command-line options.
  struct Options {
    /// Options for the 'clean' tool
    struct Clean {
      Clean();
      /// True if we should clean all built files, including
      /// those created by generator rules. False to clean all
      /// built files excluding those created by generator rules.
      bool generator;

      /// True to interpret "targets" as a list of rules instead
      /// of as a list of targets to clean.
      bool targets_are_rules;

    };
    struct Commands {
      enum PrintCommandMode {
        /// Only print the final command to build a target
        /// not the entire chain.
        PCM_Single,
        /// Print the full chain of commands to build a target
        PCM_All
      };
      Commands();
      /// The mode to use when printing the commands.
      PrintCommandMode mode;
    };
    struct CompilationDatabase {
      enum EvaluateCommandMode {
        /// Normal mode - does not expand @rspfile invocations.
        ECM_NORMAL,
        /// Expand @rspfile style response file invocations.
        ECM_EXPAND_RSPFILE
      };
      CompilationDatabase();
      /// The mode for evaluating commands
      EvaluateCommandMode eval_mode;
    };
    struct MSVC {
      MSVC();

      std::string deps_prefix;
      std::string envfile;
      std::string output_filename;
    };
    struct Rules {
      Rules();
      /// Whether or not to print the rules description
      bool print_description;
    };
    struct Targets {
      /// The mode to use when listing the targets
      enum TargetsMode {
        /// Show all targets
        TM_ALL,
        /// List targets by depth in the DAG
        TM_DEPTH,
        /// List targets by rule
        TM_RULE,
      };

      Targets();
      /// The max depth to list targets when using the TM_DEPTH mode.
      int depth;
      /// The mode to use when listing targets
      TargetsMode mode;
      /// The name of the rule to use when listing targets
      /// with the TM_RULE mode.
      std::string rule;
    };
    Options();

    /// Options to use when using the 'clean' tool.
    Clean clean_options;

    /// Options to use when using the 'commands' tool.
    Commands commands_options;

    /// Options to use when using the 'compdb' tool.
    CompilationDatabase compilationdatabase_options;

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

    /// Options to use when using the 'msvc' tool.
    MSVC msvc_options;

    /// The level of parallelism to use during the build
    int parallelism;
 
    /// Whether phony cycles should warn or print an error.
    bool phony_cycle_should_err;
  
    /// Options to use when using the 'rules' tool.
    Rules rules_options;

    /// The list of targets to apply the selected tool to. This
    /// is not used by all tools, and so can reasonably default to
    /// being an empty list.
    std::vector<std::string> targets;

    /// Options to use when using the 'targets' tool.
    Targets targets_options;

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
  Execution(const char* ninja_command, Options options, std::unique_ptr<Logger> logger);
  Execution(const char* ninja_command, Options options, std::unique_ptr<Logger> logger, Status* status);

  /// Browse the dependency graph using a webbrowser. This will
  /// launch a separate Python process to service requests.
  /// @return 0 on success.
  int Browse();
  /// Use ninja to build a project. This is the main reason to use ninja.
  /// @return 0 on success.
  int Build();
  /// Clean (delete) intermediate build output.
  /// @return 0 on success.
  int Clean();
  /// Show the commands required to build a given target.
  /// @return 0 on success.
  int Commands();
  /// Dump JSON compilation database to stdout.
  /// @return 0 on success.
  int CompilationDatabase();
  /// Show dependencies stored in the deps log.
  /// @return 0 on success.
  int Deps();
  /// Output a graphviz dot file for targets
  /// @return 0 on success.
  int Graph();
  /// Experimental. Build helper for MSVC cl.exe.
  /// @return 0 on success.
  int MSVC();
  /// Show inputs/outputs for a path.
  /// @return 0 on success.
  int Query();
  /// Recompacts ninja-internal data structures.
  /// @return 0 on success.
  int Recompact();
  /// List all rules.
  /// @return 0 on success.
  int Rules();
  /// List targets by their rule or depth in the DAG.
  /// @return 0 on success.
  int Targets();
  /// Easter egg.
  /// @return 0 on success.
  int Urtle();

protected:
  /// Helper function for tools to allow them to change
  /// to the current working directory.
  /// @return true on success.
  bool ChangeToWorkingDirectory();
  /// Perform the inner loop for the work of doing a build.
  /// @return true on success.
  bool DoBuild();

  /// Dump the metrics about the build requested by '-d stats'.
  void DumpMetrics();

  /// Create the build dir if it does not exist.
  /// @return true on success.
  bool EnsureBuildDirExists(std::string* err);

  /// Load the parser, build log and deps log. Also creates
  /// the build dir if necessary.
  /// @return true on success.
  bool LoadLogs();
  /// Loads the manifest parser.
  /// @return true on success.
  bool LoadParser(const std::string& input_file);

  /// Log an error message.
  void LogError(const std::string& message);
  /// Log an info message.
  void LogInfo(const std::string& message);
  /// Log an warning message.
  void LogWarning(const std::string& message);

  /// Open the build log.
  /// @return false on error.
  bool OpenBuildLog(bool recompact_only, std::string* err);
  
  /// Open the deps log: load it, then open for writing.
  /// @return false on error.
  bool OpenDepsLog(bool recompact_only, std::string* err);
  
  /// Rebuild the manifest, if necessary.
  /// Fills in \a err on error.
  /// @return true if the manifest was rebuilt.
  bool RebuildManifest(const char* input_file, std::string* err);
  
  void ToolTargetsList();
  void ToolTargetsList(const std::string& rule_name);

  /// Build configuration set from flags (e.g. parallelism).
  BuildConfig config_;

  // The command used to run ninja.
  const char* ninja_command_;

  /// The options provided to this execution when it is built
  /// that control most of the execution's behavior.
  Options options_;

  /// The current state of the build.
  State* state_;

  /// The status printer to use when showing status.
  Status* status_;
};

}  // namespace ninja

#endif // NINJA_PUBLIC_EXECUTION_H_
