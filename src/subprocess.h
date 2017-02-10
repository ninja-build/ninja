// Copyright 2012 Google Inc. All Rights Reserved.
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

#ifndef NINJA_SUBPROCESS_H_
#define NINJA_SUBPROCESS_H_

#include <string>
#include <vector>
#include <queue>
using namespace std;

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#endif

// ppoll() exists on FreeBSD, but only on newer versions.
#ifdef __FreeBSD__
#  include <sys/param.h>
#  if defined USE_PPOLL && __FreeBSD_version < 1002000
#    undef USE_PPOLL
#  endif
#endif

#include "exit_status.h"

#ifdef _WIN32
// On Windows, stderr and stdout must be captured separately,
// because otherwise the output of both streams may be mixed
// and hence be corrupt
enum PipeType { PIPE_StdOut = 0, PIPE_StdErr = 1};
#endif

/// Subprocess wraps a single async subprocess.  It is entirely
/// passive: it expects the caller to notify it when its fds are ready
/// for reading, as well as call Finish() to reap the child once done()
/// is true.
struct Subprocess {
  ~Subprocess();

  /// Returns ExitSuccess on successful process exit, ExitInterrupted if
  /// the process was interrupted, ExitFailure if it otherwise failed.
  ExitStatus Finish();

  bool Done() const;

  string GetOutput() const;

 private:
  Subprocess(bool use_console);
  bool Start(struct SubprocessSet* set, const string& command);

#ifdef _WIN32
  void OnPipeReady(PipeType pipe_type);
  string buf_[2];
  /// Set up pipe_ as the parent-side pipe of the subprocess; return the
  /// other end of the pipe, usable in the child process.
  HANDLE SetupPipe(HANDLE ioport, PipeType pipe_type);

  HANDLE child_;
  HANDLE pipe_[2];
  OVERLAPPED overlapped_[2];
  char overlapped_buf_[2][4 << 10];
  bool is_reading_[2];
#else
  void OnPipeReady();
  string buf_;

  int fd_;
  pid_t pid_;
#endif
  bool use_console_;

  friend struct SubprocessSet;
};

/// SubprocessSet runs a ppoll/pselect() loop around a set of Subprocesses.
/// DoWork() waits for any state change in subprocesses; finished_
/// is a queue of subprocesses as they finish.
struct SubprocessSet {
  SubprocessSet();
  ~SubprocessSet();

  Subprocess* Add(const string& command, bool use_console = false);
  bool DoWork();
  Subprocess* NextFinished();
  void Clear();

  vector<Subprocess*> running_;
  queue<Subprocess*> finished_;

#ifdef _WIN32
  static BOOL WINAPI NotifyInterrupted(DWORD dwCtrlType);
  static HANDLE ioport_;
#else
  static void SetInterruptedFlag(int signum);
  static void HandlePendingInterruption();
  /// Store the signal number that causes the interruption.
  /// 0 if not interruption.
  static int interrupted_;

  static bool IsInterrupted() { return interrupted_ != 0; }

  struct sigaction old_int_act_;
  struct sigaction old_term_act_;
  struct sigaction old_hup_act_;
  sigset_t old_mask_;
#endif
};

#endif // NINJA_SUBPROCESS_H_
