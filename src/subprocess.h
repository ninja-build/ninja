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

// Subprocess wraps a single async subprocess.  It is entirely
// passive: it expects the caller to notify it when its fds are ready
// for reading, as well as call Finish() to reap the child once done()
// is true.
struct Subprocess {
  Subprocess();
  ~Subprocess();
  bool Start(const string& command);
  void OnFDReady(int fd);
  // Returns true on successful process exit.
  bool Finish();

  bool done() const {
    return stdout_.fd_ == -1 && stderr_.fd_ == -1;
  }

  struct Stream {
    Stream();
    ~Stream();
    int fd_;
    string buf_;
  };
  Stream stdout_, stderr_;
  pid_t pid_;
};

// SubprocessSet runs a poll() loop around a set of Subprocesses.
// DoWork() waits for any state change in subprocesses; finished_
// is a queue of subprocesses as they finish.
struct SubprocessSet {
  void Add(Subprocess* subprocess);
  void DoWork();
  Subprocess* NextFinished();

  vector<Subprocess*> running_;
  queue<Subprocess*> finished_;
};

#endif // NINJA_SUBPROCESS_H_
