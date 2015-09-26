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

#include "failed_commands_script.h"

#include "util.h"
#include "test.h"

#include <sys/stat.h>
#ifdef _WIN32
#include <fcntl.h>
#include <share.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

const char kTestFilename[] = "FailedCommandsScriptTest-tempfile";

struct FailedCommandsScriptTest : public StateTestWithBuiltinRules {
  virtual void SetUp() {
    // In case a crashing test left a stale file behind.
    unlink(kTestFilename);
  }
  virtual void TearDown() {
    unlink(kTestFilename);
  }
};

TEST_F(FailedCommandsScriptTest, Write) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule CMD\n"
"     command = $cmd\n"
"     description = $desc $out\n"
"\n"
"build out1 out11: CMD in1\n"
"      cmd = echo 1\n"
"      desc = cmd1 desc\n"
"\n"
"build out2: CMD in2 in22\n"
"      cmd = echo 2\n"
"      desc = cmd2 desc\n"));

  string err;
  vector<Edge*> edges;
  edges.push_back(state_.edges_[0]);
  edges.push_back(state_.edges_[1]);
  ASSERT_TRUE(WriteFailedCommandsScript(kTestFilename, edges, &err));

  string contents;
  ASSERT_EQ(::ReadFile(kTestFilename, &contents, &err), 0);
  // printf(">%s<", contents.c_str());
  // it is a shell script.
  ASSERT_EQ(contents.find("#!/bin/sh\n"), 0u);
  ASSERT_NE(contents.find("\nset -e\n"), string::npos);
  // rule are present.
  ASSERT_NE(contents.find("\n# rule: CMD\n"), string::npos);
  // commands are present.
  ASSERT_NE(contents.find("\necho 1\n"), string::npos);
  ASSERT_NE(contents.find("\necho 1\n"), string::npos);
  // descriptions are present.
  ASSERT_NE(contents.find("# description:\n"), string::npos);
  ASSERT_NE(contents.find(" cmd1 desc out1 out11\n"), string::npos);
  ASSERT_NE(contents.find(" cmd2 desc out2\n"), string::npos);
  // inputs are present.
  ASSERT_NE(contents.find(" in1\n"), string::npos);
  ASSERT_NE(contents.find(" in2\n"), string::npos);
  ASSERT_NE(contents.find(" in22\n"), string::npos);
  // outputs are present.
  ASSERT_NE(contents.find(" out1\n"), string::npos);
  ASSERT_NE(contents.find(" out11\n"), string::npos);
  ASSERT_NE(contents.find(" out2\n"), string::npos);
  // ends with a newline character
  ASSERT_EQ(contents.rfind("\n"), contents.size() - 1);
}

TEST_F(FailedCommandsScriptTest, WriteNoDescription) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule CMD\n"
"     command = echo\n"
"build out1 out11: CMD in1\n"));

  string err;
  vector<Edge*> edges;
  edges.push_back(state_.edges_[0]);
  ASSERT_TRUE(WriteFailedCommandsScript(kTestFilename, edges, &err));

  string contents;
  ASSERT_EQ(::ReadFile(kTestFilename, &contents, &err), 0);
  // printf(">%s<", contents.c_str());
  // description is not present.
  ASSERT_NE(contents.find("# description:\n#   \n"), string::npos);
}

}  // anonymous namespace
