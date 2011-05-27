// Copyright 2011 Google Inc. All Rights Reserved.
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
#endif

struct SubprocessSet;

/// Subprocess wraps a single async subprocess.  It is entirely
/// passive: it expects the caller to notify it when its fds are ready
/// for reading, as well as call Finish() to reap the child once done()
/// is true.
struct Subprocess {
  Subprocess();
  ~Subprocess();
  bool Start(SubprocessSet* set, const string& command);
  void OnPipeReady();
  /// Returns true on successful process exit.
  bool Finish();

  bool Done() const;

  const string& GetOutput() const;

 private:
  string buf_;

#ifdef _WIN32
  /// Set up pipe_ as the parent-side pipe of the subprocess; return the
  /// other end of the pipe, usable in the child process.
  HANDLE SetupPipe(HANDLE ioport);

  HANDLE child_;
  HANDLE pipe_;
  OVERLAPPED overlapped_;
  char overlapped_buf_[4 << 10];
#else
  int fd_;
  pid_t pid_;
#endif

  friend struct SubprocessSet;
};

/// SubprocessSet runs a poll() loop around a set of Subprocesses.
/// DoWork() waits for any state change in subprocesses; finished_
/// is a queue of subprocesses as they finish.
struct SubprocessSet {
  SubprocessSet();
  ~SubprocessSet();

  void Add(Subprocess* subprocess);
  void DoWork();
  Subprocess* NextFinished();

  vector<Subprocess*> running_;
  queue<Subprocess*> finished_;

#ifdef _WIN32
  HANDLE ioport_;
#endif
};

#endif // NINJA_SUBPROCESS_H_
