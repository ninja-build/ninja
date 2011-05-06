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

#include "subprocess.h"

#include "test.h"

namespace {

#ifdef _WIN32
const char* kSimpleCommand = "dir \\";
#else
const char* kSimpleCommand = "ls /";
#endif

struct SubprocessTest : public testing::Test {
  SubprocessSet subprocs_;
};

}  // anonymous namespace

// Run a command that succeeds and emits to stdout.
TEST_F(SubprocessTest, GoodCommandStdout) {
  Subprocess ls;
  EXPECT_TRUE(ls.Start(&subprocs_, kSimpleCommand));

  while (!ls.Done()) {
    // Pretend we discovered that stdout was ready for writing.
    ls.OnPipeReady();
  }

  EXPECT_TRUE(ls.Finish());
  EXPECT_NE("", ls.GetOutput());
}

// Run a command that fails and emits to stderr.
TEST_F(SubprocessTest, BadCommandStderr) {
  Subprocess subproc;
  EXPECT_TRUE(subproc.Start(&subprocs_, "ninja_no_such_command"));

  while (!subproc.Done()) {
    // Pretend we discovered that stderr was ready for writing.
    subproc.OnPipeReady();
  }

  EXPECT_FALSE(subproc.Finish());
  EXPECT_NE("", subproc.GetOutput());
}

TEST_F(SubprocessTest, SetWithSingle) {
  Subprocess* subproc = new Subprocess;
  EXPECT_TRUE(subproc->Start(&subprocs_, kSimpleCommand));
  subprocs_.Add(subproc);

  while (!subproc->Done()) {
    subprocs_.DoWork();
  }
  ASSERT_TRUE(subproc->Finish());
  ASSERT_NE("", subproc->GetOutput());

  ASSERT_EQ(1u, subprocs_.finished_.size());
}

TEST_F(SubprocessTest, SetWithMulti) {
  Subprocess* processes[3];
  const char* kCommands[3] = {
    kSimpleCommand,
#ifdef _WIN32
    "echo hi",
    "time /t",
#else
    "whoami",
    "pwd",
#endif
  };

  for (int i = 0; i < 3; ++i) {
    processes[i] = new Subprocess;
    EXPECT_TRUE(processes[i]->Start(&subprocs_, kCommands[i]));
    subprocs_.Add(processes[i]);
  }

  ASSERT_EQ(3u, subprocs_.running_.size());
  for (int i = 0; i < 3; ++i) {
    ASSERT_FALSE(processes[i]->Done());
    ASSERT_EQ("", processes[i]->GetOutput());
  }

  while (!processes[0]->Done() || !processes[1]->Done() ||
         !processes[2]->Done()) {
    ASSERT_GT(subprocs_.running_.size(), 0u);
    subprocs_.DoWork();
  }

  ASSERT_EQ(0u, subprocs_.running_.size());
  ASSERT_EQ(3u, subprocs_.finished_.size());

  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(processes[i]->Finish());
    ASSERT_NE("", processes[i]->GetOutput());
    delete processes[i];
  }
}

