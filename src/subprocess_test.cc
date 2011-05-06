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

TEST(Subprocess, Ls) {
  Subprocess ls;
  EXPECT_TRUE(ls.Start(NULL, "ls /"));

  // Pretend we discovered that stdout was ready for writing.
  ls.OnPipeReady();

  EXPECT_TRUE(ls.Finish());
  EXPECT_NE("", ls.GetOutput());
}

TEST(Subprocess, BadCommand) {
  Subprocess subproc;
  EXPECT_TRUE(subproc.Start(NULL, "ninja_no_such_command"));

  // Pretend we discovered that stderr was ready for writing.
  subproc.OnPipeReady();

  EXPECT_FALSE(subproc.Finish());
  EXPECT_NE("", subproc.GetOutput());
}

TEST(SubprocessSet, Single) {
  SubprocessSet subprocs;
  Subprocess* ls = new Subprocess;
  EXPECT_TRUE(ls->Start(NULL, "ls /"));
  subprocs.Add(ls);

  while (!ls->Done()) {
    subprocs.DoWork();
  }
  ASSERT_TRUE(ls->Finish());
  ASSERT_NE("", ls->GetOutput());

  ASSERT_EQ(1u, subprocs.finished_.size());
}

TEST(SubprocessSet, Multi) {
  SubprocessSet subprocs;
  Subprocess* processes[3];
  const char* kCommands[3] = {
    "ls /",
    "whoami",
    "pwd",
  };

  for (int i = 0; i < 3; ++i) {
    processes[i] = new Subprocess;
    EXPECT_TRUE(processes[i]->Start(NULL, kCommands[i]));
    subprocs.Add(processes[i]);
  }

  ASSERT_EQ(3u, subprocs.running_.size());
  for (int i = 0; i < 3; ++i) {
    ASSERT_FALSE(processes[i]->Done());
    ASSERT_EQ("", processes[i]->GetOutput());
  }

  while (!processes[0]->Done() || !processes[1]->Done() ||
         !processes[2]->Done()) {
    ASSERT_GT(subprocs.running_.size(), 0u);
    subprocs.DoWork();
  }

  ASSERT_EQ(0u, subprocs.running_.size());
  ASSERT_EQ(3u, subprocs.finished_.size());

  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(processes[i]->Finish());
    ASSERT_NE("", processes[i]->GetOutput());
    delete processes[i];
  }
}

