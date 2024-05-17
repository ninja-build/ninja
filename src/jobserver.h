// Copyright 2024 Google Inc. All Rights Reserved.
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

#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdbool>
#include <string>

/// Jobserver limits parallelism by acquiring tokens from an external
/// pool before running commands. On posix systems the pool is a fifo filled
/// with N characters. On windows systems the pool is a semaphore initialized
/// to N. When a command is finished, the acquired token is released by writing
/// it back to fifo / increasing the semaphore count.
///
/// The jobserver functionality is enabled by passing
/// --jobserver-auth=<type>:<val> in the MAKEFLAGS environment variable. On
/// posix systems, <type> is 'fifo' and <val> is a path to a fifo. On windows
/// systems, <type> is 'sem' and <val> is the name of a semaphore.
///
/// The class is used in build.cc by calling Init() to parse the MAKEFLAGS
/// argument and open the fifo / semaphore. Once enabled, Acquire() must be
/// called to try to acquire a token from the pool. If a token is acquired, a
/// new command can be started. Once the command is completed, Release() must
/// be called to return the token to the pool.
///
/// Note that this class implements the jobserver functionality from GNU make
/// v4.4 and later. Older versions of make passes open pipe file descriptors
/// to sub-makes and specifies the file descriptor numbers using
/// --jobserver-auth=R,W in the MAKEFLAGS environment variable. The older pipe
/// method is deliberately not implemented here, as it is not as simple as the
/// fifo method.
struct Jobserver {
  ~Jobserver();

  /// Parse the MAKEFLAGS environment variable to receive the path / name of the
  /// token pool, and open the handle to the pool. If a jobserver argument is
  /// found in the MAKEFLAGS environment variable, and the handle is
  /// successfully opened, subsequent calls to Enable() returns true.
  /// If a jobserver argument is found, but the handle fails to be opened, the
  /// ninja process is aborted with an error.
  void Init();

  /// Return true if jobserver functionality is enabled and initialized.
  bool Enabled() const;

  /// Try to to acquire a token from the external token pool (without blocking).
  /// Should be called every time Ninja needs to start a command process.
  /// Return true on success (token acquired), and false on failure (no tokens
  /// available). First call always succeeds.
  bool Acquire();

  /// Return a previously acquired token to the external token pool. Must be
  /// called for any _successful_ call to Acquire(). Normally when a command
  /// subprocess completes, or when Ninja itself exits, even in case of errors.
  void Release();

protected:
  /// Parse the --jobserver-auth argument from the MAKEFLAGS environment
  /// variable. Return true if the argument is found and correctly parsed.
  /// Return false if the argument is not found, or fails to parse.
  bool ParseJobserverAuth(const char* type);

  /// Path to the fifo on posix systems or name of the semaphore on windows
  /// systems.
  std::string jobserver_name_;

private:
  /// Implementation specific method to acquire a token from the external pool,
  /// which is called for all but the first requested tokens.
  bool AcquireToken();

  /// Implementation specific method to release a token to the external pool,
  /// which is called for all but the last released tokens.
  void ReleaseToken();

  /// Number of currently acquired tokens. Used to know when the first (free)
  /// token has been acquired / released, and to verify that all acquired tokens
  /// have been released before exiting.
  size_t token_count_ = 0;

#ifdef _WIN32
  /// Handle to the opened semaphore used as external token pool.
  HANDLE sem_ = INVALID_HANDLE_VALUE;
#else
  /// Non-blocking file descriptor for the opened fifo used as the external
  /// token pool.
  int fd_ = -1;
#endif
};
