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

#include <string>

/// The GNU jobserver limits parallelism by assigning a token from an external
/// pool for each command. On posix systems, the pool is a fifo or simple pipe
/// with N characters. On windows systems, the pool is a semaphore initialized
/// to N. When a command is finished, the acquired token is released by writing
/// it back to the fifo or pipe or by increasing the semaphore count.
///
/// The jobserver functionality is enabled by passing --jobserver-auth=<val>
/// (previously --jobserver-fds=<val> in older versions of Make) in the MAKEFLAGS
/// environment variable and creating the respective file descriptors or objects.
/// On posix systems, <val> is 'fifo:<name>' or '<read_fd>,<write_fd>' for pipes.
/// On windows systems, <val> is the name of the semaphore.
///
/// The classes parse the MAKEFLAGS variable and opens an object handle if needed.
/// Once enabled, Acquire() must be called to acquire a token from the pool.
/// If a token is acquired, a new command can be started.
/// Once the command is completed, Release() must be called to return a token.
/// The token server does not care in which order a token is received.
struct Jobserver {
  /// Return current token count or initialization signal if negative.
  int Tokens() const { return token_count_; }

  /// Read MAKEFLAGS environment variable and process the jobserver flag value.
  virtual void Parse() {};

  /// Return true if jobserver functionality is enabled and initialized.
  virtual bool Enabled() const { return false; }

  /// Implementation-specific method to acquire a token from the external pool
  /// which is called for all tokens but returns early for the first token.
  /// This method is called every time Ninja needs to start a command process.
  /// Return a non-NUL char value on success (token acquired), and '\0' on failure.
  /// First call always succeeds. Ninja is aborted on read errors from a fifo pipe.
  /// The return is the token character to be saved for release after work is done.
  virtual unsigned char Acquire() { return '\0'; }

  /// Implementation-specific method to release a token to the external pool
  /// which is called for all tokens but returns early for the last token.
  /// The parameter is the token returned by Acquire() to be sent to the token server.
  /// A token with the default value of '\0' will not be sent to the token server.
  /// After sent, the token that the pointer parameter points to is cleared to '\0'.
  /// It must be called for each successful call to Acquire() after the command exits,
  /// even if subprocesses fail or in the case of errors causing Ninja to exit.
  /// Ninja is aborted on write errors, and calls always decrement token count.
  virtual void Release(unsigned char*) {};

 protected:
  /// The number of currently acquired tokens, or a status signal if negative.
  /// This is used to estimate the load capacity for attempting to start a new job,
  /// and when the implicit (first) token has been acquired (initialization).
  /// -1: initialized without a token
  ///  0: uninitialized or disabled
  /// +n: number of tokens in use
  int token_count_ = 0;

  /// Whether a pipe to the jobserver token pool is closed
  /// when it is expected to be open based on MAKEFLAGS
  /// (e.g. subcommands not marked recursive, environment passed),
  /// or the pipe is closed when expected to be closed
  /// but when the parent process is jobserver-capable
  /// (e.g. the parent jobserver process build is non-parallel).
  bool jobserver_closed_ = false;

  /// String of the parsed value of the jobserver flag passed to environment.
  std::string jobserver_name_;

  /// Substrings for parsing MAKEFLAGS environment variable.
  static constexpr char const kAuthKey[] = "--jobserver-auth=";
  static constexpr char const kFdsKey[]  = "--jobserver-fds=";
  static constexpr char const kFifoKey[] = "fifo:";
};

struct PosixJobserverClient : public Jobserver {
  /// Parse the MAKEFLAGS environment variable to receive the path / FDs
  /// of the token pool, and open the handle to the pool if it is an object.
  /// If a jobserver argument is found in the MAKEFLAGS environment variable,
  /// and the handle is successfully opened, later calls to Enable() return true.
  /// If a jobserver argument is found, but the handle fails to be opened,
  /// the ninja process is aborted with an error, or, when the FDs provided are bad
  /// the build falls back to non-parallel building and the client does not read.
  explicit PosixJobserverClient();

  void Parse() override;
  bool Enabled() const override;
  unsigned char Acquire() override;
  void Release(unsigned char*) override;

 private:
  /// Whether the type of jobserver pipe supplied to ninja is named.
  bool jobserver_fifo_ = false;

  /// File descriptors to communicate with upstream jobserver token pool.
  int rfd_ = -1;
  int wfd_ = -1;
};
