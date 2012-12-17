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

#include "deps_log.h"

#include "graph.h"
#include "util.h"
#include "test.h"

namespace {

const char kTestFilename[] = "DepsLogTest-tempfile";

struct DepsLogTest : public testing::Test {
  virtual void SetUp() {
    // In case a crashing test left a stale file behind.
    unlink(kTestFilename);
  }
  virtual void TearDown() {
    //unlink(kTestFilename);
  }
};

TEST_F(DepsLogTest, WriteRead) {
  State state1;
  DepsLog log1;
  string err;
  EXPECT_TRUE(log1.OpenForWrite(kTestFilename, &err));
  ASSERT_EQ("", err);

  vector<Node*> deps;
  deps.push_back(state1.GetNode("foo.h"));
  deps.push_back(state1.GetNode("bar.h"));
  log1.RecordDeps(state1.GetNode("out.o"), 1, deps);

  deps.clear();
  deps.push_back(state1.GetNode("foo.h"));
  deps.push_back(state1.GetNode("bar2.h"));
  log1.RecordDeps(state1.GetNode("out2.o"), 2, deps);

  log1.Close();

  State state2;
  DepsLog log2;
  EXPECT_TRUE(log1.Load(kTestFilename, &state2, &err));
  ASSERT_EQ("", err);
  state2.Dump();

  state2.GetNode("out2.o")->Dump();
}

}  // anonymous namespace
