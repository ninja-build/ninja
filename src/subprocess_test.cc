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

#include "subprocess.h"
#include "tokenpool.h"

#include "test.h"

#ifndef _WIN32
// SetWithLots need setrlimit.
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

using namespace std;

namespace {

#ifdef _WIN32
const char* kSimpleCommand = "cmd /c dir \\";
#else
const char* kSimpleCommand = "ls /";
#endif

struct TestTokenPool : public TokenPool {
  bool Acquire()     { return false; }
  void Reserve()     {}
  void Release()     {}
  void Clear()       {}
  bool SetupClient(bool ignore_unused, bool verbose,
                   double& max_load_average) { return false; }
  bool SetupMaster(bool verbose,
                   int parallelism,
                   double max_load_average,
                   const char* style)  { return false; }

#ifdef _WIN32
  bool _token_available;
  void WaitForTokenAvailability(HANDLE ioport) {
    if (_token_available)
      // unblock GetQueuedCompletionStatus()
      PostQueuedCompletionStatus(ioport, 0, (ULONG_PTR) this, NULL);
  }
  bool TokenIsAvailable(ULONG_PTR key) { return key == (ULONG_PTR) this; }
#else
  int _fd;
  int GetMonitorFd() { return _fd; }
#endif
};

struct SubprocessTest : public testing::Test {
  SubprocessSet subprocs_;
  TestTokenPool tokens_;
};

}  // anonymous namespace

// Run a command that fails and emits to stderr.
TEST_F(SubprocessTest, BadCommandStderr) {
  Subprocess* subproc = subprocs_.Add("cmd /c ninja_no_such_command");
  ASSERT_NE((Subprocess *) 0, subproc);

  subprocs_.ResetTokenAvailable();
  while (!subproc->Done()) {
    // Pretend we discovered that stderr was ready for writing.
    subprocs_.DoWork(NULL);
  }
  ASSERT_FALSE(subprocs_.IsTokenAvailable());

  EXPECT_EQ(ExitFailure, subproc->Finish());
  EXPECT_NE("", subproc->GetOutput());
}

// Run a command that does not exist
TEST_F(SubprocessTest, NoSuchCommand) {
  Subprocess* subproc = subprocs_.Add("ninja_no_such_command");
  ASSERT_NE((Subprocess *) 0, subproc);

  subprocs_.ResetTokenAvailable();
  while (!subproc->Done()) {
    // Pretend we discovered that stderr was ready for writing.
    subprocs_.DoWork(NULL);
  }
  ASSERT_FALSE(subprocs_.IsTokenAvailable());

  EXPECT_EQ(ExitFailure, subproc->Finish());
  EXPECT_NE("", subproc->GetOutput());
#ifdef _WIN32
  ASSERT_EQ("CreateProcess failed: The system cannot find the file "
            "specified.\n", subproc->GetOutput());
#endif
}

#ifndef _WIN32

TEST_F(SubprocessTest, InterruptChild) {
  Subprocess* subproc = subprocs_.Add("kill -INT $$");
  ASSERT_NE((Subprocess *) 0, subproc);

  subprocs_.ResetTokenAvailable();
  while (!subproc->Done()) {
    subprocs_.DoWork(NULL);
  }
  ASSERT_FALSE(subprocs_.IsTokenAvailable());

  EXPECT_EQ(ExitInterrupted, subproc->Finish());
}

TEST_F(SubprocessTest, InterruptParent) {
  Subprocess* subproc = subprocs_.Add("kill -INT $PPID ; sleep 1");
  ASSERT_NE((Subprocess *) 0, subproc);

  while (!subproc->Done()) {
    bool interrupted = subprocs_.DoWork(NULL);
    if (interrupted)
      return;
  }

  ASSERT_FALSE("We should have been interrupted");
}

TEST_F(SubprocessTest, InterruptChildWithSigTerm) {
  Subprocess* subproc = subprocs_.Add("kill -TERM $$");
  ASSERT_NE((Subprocess *) 0, subproc);

  subprocs_.ResetTokenAvailable();
  while (!subproc->Done()) {
    subprocs_.DoWork(NULL);
  }
  ASSERT_FALSE(subprocs_.IsTokenAvailable());

  EXPECT_EQ(ExitInterrupted, subproc->Finish());
}

TEST_F(SubprocessTest, InterruptParentWithSigTerm) {
  Subprocess* subproc = subprocs_.Add("kill -TERM $PPID ; sleep 1");
  ASSERT_NE((Subprocess *) 0, subproc);

  while (!subproc->Done()) {
    bool interrupted = subprocs_.DoWork(NULL);
    if (interrupted)
      return;
  }

  ASSERT_FALSE("We should have been interrupted");
}

TEST_F(SubprocessTest, InterruptChildWithSigHup) {
  Subprocess* subproc = subprocs_.Add("kill -HUP $$");
  ASSERT_NE((Subprocess *) 0, subproc);

  subprocs_.ResetTokenAvailable();
  while (!subproc->Done()) {
    subprocs_.DoWork(NULL);
  }
  ASSERT_FALSE(subprocs_.IsTokenAvailable());

  EXPECT_EQ(ExitInterrupted, subproc->Finish());
}

TEST_F(SubprocessTest, InterruptParentWithSigHup) {
  Subprocess* subproc = subprocs_.Add("kill -HUP $PPID ; sleep 1");
  ASSERT_NE((Subprocess *) 0, subproc);

  while (!subproc->Done()) {
    bool interrupted = subprocs_.DoWork(NULL);
    if (interrupted)
      return;
  }

  ASSERT_FALSE("We should have been interrupted");
}

TEST_F(SubprocessTest, Console) {
  // Skip test if we don't have the console ourselves.
  if (isatty(0) && isatty(1) && isatty(2)) {
    Subprocess* subproc =
        subprocs_.Add("test -t 0 -a -t 1 -a -t 2", /*use_console=*/true);
    ASSERT_NE((Subprocess*)0, subproc);

    subprocs_.ResetTokenAvailable();
    while (!subproc->Done()) {
      subprocs_.DoWork(NULL);
    }
    ASSERT_FALSE(subprocs_.IsTokenAvailable());

    EXPECT_EQ(ExitSuccess, subproc->Finish());
  }
}

#endif

TEST_F(SubprocessTest, SetWithSingle) {
  Subprocess* subproc = subprocs_.Add(kSimpleCommand);
  ASSERT_NE((Subprocess *) 0, subproc);

  subprocs_.ResetTokenAvailable();
  while (!subproc->Done()) {
    subprocs_.DoWork(NULL);
  }
  ASSERT_FALSE(subprocs_.IsTokenAvailable());
  ASSERT_EQ(ExitSuccess, subproc->Finish());
  ASSERT_NE("", subproc->GetOutput());

  ASSERT_EQ(1u, subprocs_.finished_.size());
}

TEST_F(SubprocessTest, SetWithMulti) {
  Subprocess* processes[3];
  const char* kCommands[3] = {
    kSimpleCommand,
#ifdef _WIN32
    "cmd /c echo hi",
    "cmd /c time /t",
#else
    "id -u",
    "pwd",
#endif
  };

  for (int i = 0; i < 3; ++i) {
    processes[i] = subprocs_.Add(kCommands[i]);
    ASSERT_NE((Subprocess *) 0, processes[i]);
  }

  ASSERT_EQ(3u, subprocs_.running_.size());
  for (int i = 0; i < 3; ++i) {
    ASSERT_FALSE(processes[i]->Done());
    ASSERT_EQ("", processes[i]->GetOutput());
  }

  subprocs_.ResetTokenAvailable();
  while (!processes[0]->Done() || !processes[1]->Done() ||
         !processes[2]->Done()) {
    ASSERT_GT(subprocs_.running_.size(), 0u);
    subprocs_.DoWork(NULL);
  }
  ASSERT_FALSE(subprocs_.IsTokenAvailable());
  ASSERT_EQ(0u, subprocs_.running_.size());
  ASSERT_EQ(3u, subprocs_.finished_.size());

  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ(ExitSuccess, processes[i]->Finish());
    ASSERT_NE("", processes[i]->GetOutput());
    delete processes[i];
  }
}

#if defined(USE_PPOLL)
TEST_F(SubprocessTest, SetWithLots) {
  // Arbitrary big number; needs to be over 1024 to confirm we're no longer
  // hostage to pselect.
  const unsigned kNumProcs = 1025;

  // Make sure [ulimit -n] isn't going to stop us from working.
  rlimit rlim;
  ASSERT_EQ(0, getrlimit(RLIMIT_NOFILE, &rlim));
  if (rlim.rlim_cur < kNumProcs) {
    printf("Raise [ulimit -n] above %u (currently %lu) to make this test go\n",
           kNumProcs, rlim.rlim_cur);
    return;
  }

  vector<Subprocess*> procs;
  for (size_t i = 0; i < kNumProcs; ++i) {
    Subprocess* subproc = subprocs_.Add("/bin/echo");
    ASSERT_NE((Subprocess *) 0, subproc);
    procs.push_back(subproc);
  }
  subprocs_.ResetTokenAvailable();
  while (!subprocs_.running_.empty())
    subprocs_.DoWork(NULL);
  ASSERT_FALSE(subprocs_.IsTokenAvailable());
  for (size_t i = 0; i < procs.size(); ++i) {
    ASSERT_EQ(ExitSuccess, procs[i]->Finish());
    ASSERT_NE("", procs[i]->GetOutput());
  }
  ASSERT_EQ(kNumProcs, subprocs_.finished_.size());
}
#endif  // !__APPLE__ && !_WIN32

// TODO: this test could work on Windows, just not sure how to simply
// read stdin.
#ifndef _WIN32
// Verify that a command that attempts to read stdin correctly thinks
// that stdin is closed.
TEST_F(SubprocessTest, ReadStdin) {
  Subprocess* subproc = subprocs_.Add("cat -");
  subprocs_.ResetTokenAvailable();
  while (!subproc->Done()) {
    subprocs_.DoWork(NULL);
  }
  ASSERT_FALSE(subprocs_.IsTokenAvailable());
  ASSERT_EQ(ExitSuccess, subproc->Finish());
  ASSERT_EQ(1u, subprocs_.finished_.size());
}
#endif  // _WIN32

TEST_F(SubprocessTest, TokenAvailable) {
  Subprocess* subproc = subprocs_.Add(kSimpleCommand);
  ASSERT_NE((Subprocess *) 0, subproc);

  // simulate GNUmake jobserver pipe with 1 token
#ifdef _WIN32
  tokens_._token_available = true;
#else
  int fds[2];
  ASSERT_EQ(0u, pipe(fds));
  tokens_._fd = fds[0];
  ASSERT_EQ(1u, write(fds[1], "T", 1));
#endif

  subprocs_.ResetTokenAvailable();
  subprocs_.DoWork(&tokens_);
#ifdef _WIN32
  tokens_._token_available = false;
  // we need to loop here as we have no control where the token
  // I/O completion post ends up in the queue
  while (!subproc->Done() && !subprocs_.IsTokenAvailable()) {
    subprocs_.DoWork(&tokens_);
  }
#endif

  EXPECT_TRUE(subprocs_.IsTokenAvailable());
  EXPECT_EQ(0u, subprocs_.finished_.size());

  // remove token to let DoWork() wait for command again
#ifndef _WIN32
  char token;
  ASSERT_EQ(1u, read(fds[0], &token, 1));
#endif

  while (!subproc->Done()) {
    subprocs_.DoWork(&tokens_);
  }

#ifndef _WIN32
  close(fds[1]);
  close(fds[0]);
#endif

  EXPECT_EQ(ExitSuccess, subproc->Finish());
  EXPECT_NE("", subproc->GetOutput());

  EXPECT_EQ(1u, subprocs_.finished_.size());
}

TEST_F(SubprocessTest, TokenNotAvailable) {
  Subprocess* subproc = subprocs_.Add(kSimpleCommand);
  ASSERT_NE((Subprocess *) 0, subproc);

  // simulate GNUmake jobserver pipe with 0 tokens
#ifdef _WIN32
  tokens_._token_available = false;
#else
  int fds[2];
  ASSERT_EQ(0u, pipe(fds));
  tokens_._fd = fds[0];
#endif

  subprocs_.ResetTokenAvailable();
  while (!subproc->Done()) {
    subprocs_.DoWork(&tokens_);
  }

#ifndef _WIN32
  close(fds[1]);
  close(fds[0]);
#endif

  EXPECT_FALSE(subprocs_.IsTokenAvailable());
  EXPECT_EQ(ExitSuccess, subproc->Finish());
  EXPECT_NE("", subproc->GetOutput());

  EXPECT_EQ(1u, subprocs_.finished_.size());
}
