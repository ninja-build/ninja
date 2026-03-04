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

#ifndef NINJA_BUILD_RESULT_H_
#define NINJA_BUILD_RESULT_H_

#include <string>
#include <utility>
#include <variant>

#include "exit_status.h"

struct Edge;

/// Stores the result of executing a build command
struct BuildResult {
  /// Command was completed with varying exit statuses
  struct CommandCompleted {
    Edge* edge = nullptr;
    ExitStatus status = ExitFailure;
    std::string output;

    explicit CommandCompleted(Edge* edge, ExitStatus& status,
                              std::string output)
        : edge(edge), status(status), output(std::move(output)) {}

    explicit CommandCompleted(Edge* edge, ExitStatus& status)
        : edge(edge), status(status) {}

    constexpr bool success() const { return status == ExitSuccess; }
  };

  /// Jobserver token became available while waiting for command
  struct JobserverTokenAvailable {
    ExitStatus status = ExitSuccess;
  };

  /// Interrupted while waiting
  struct Interrupted {
    ExitStatus status = ExitInterrupted;
  };

  /// No more work to be done
  struct Finished {
    // Used for DryCommandRunner and FakeCommandRunner
    ExitStatus status = ExitSuccess;
  };

  // Store the actual state as an algebraic data type
  std::variant<
    std::monostate,
    CommandCompleted,
    JobserverTokenAvailable,
    Interrupted,
    Finished> state_;

  BuildResult() = default;

  template <typename T>
  BuildResult(T&& val) : state_(std::forward<T>(val)) {}

  template <typename T>
  BuildResult& operator=(T&& val) {
    state_ = std::forward<T>(val);
    return *this;
  }

  // Helper methods
  constexpr bool finished() const {
    return std::holds_alternative<Finished>(state_);
  }

  constexpr bool interrupted() const {
    return std::holds_alternative<Interrupted>(state_);
  }

  constexpr bool jobserver_token_available() const {
    return std::holds_alternative<JobserverTokenAvailable>(state_);
  }

  constexpr bool command_completed() const {
    return std::holds_alternative<CommandCompleted>(state_);
  }

  /// Matches the internal build result state to an exit status.
  /// Dispatches based on the type of the internal state:
  ///
  /// 1. CommandCompleted: return the ExitStatus of the executed command
  /// 2. JobserverTokenAvailable: consider this as a successful exit (no work done)
  /// 3. Interrupted: return interrupted
  /// 4. Finished: consider this as a successful exit (no work done)
  constexpr ExitStatus exit_status() const {
    if (auto* cc = std::get_if<CommandCompleted>(&state_))
      return cc->status;
    if (jobserver_token_available())
      return ExitSuccess;
    if (interrupted())
      return ExitInterrupted;
    if (finished())
      return ExitSuccess;

    return ExitFailure;
  }

  /// Returns true if the build succeeded (i.e. exited with ExitSuccess),
  /// using the above exit_status() function to retrieve the status
  constexpr bool success() const { return exit_status() == ExitSuccess; }

  /// Note: runtime error to use this if command_completed() is false.
  constexpr CommandCompleted& GetCommandCompleted() {
    return std::get<CommandCompleted>(state_);
  }
};

#endif
