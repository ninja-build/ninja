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

  {
    vector<Node*> deps;
    deps.push_back(state1.GetNode("foo.h"));
    deps.push_back(state1.GetNode("bar.h"));
    log1.RecordDeps(state1.GetNode("out.o"), 1, deps);

    deps.clear();
    deps.push_back(state1.GetNode("foo.h"));
    deps.push_back(state1.GetNode("bar2.h"));
    log1.RecordDeps(state1.GetNode("out2.o"), 2, deps);
  }

  log1.Close();

  State state2;
  DepsLog log2;
  EXPECT_TRUE(log2.Load(kTestFilename, &state2, &err));
  ASSERT_EQ("", err);

  ASSERT_EQ(log1.nodes().size(), log2.nodes().size());
  for (int i = 0; i < (int)log1.nodes().size(); ++i) {
    Node* node1 = log1.nodes()[i];
    Node* node2 = log2.nodes()[i];
    ASSERT_EQ(i, node1->id());
    ASSERT_EQ(node1->id(), node2->id());
  }

  // log1 has no deps entries, as it was only used for writing.
  // Manually check the entries in log2.
  DepsLog::Deps* deps = log2.GetDeps(state2.GetNode("out.o"));
  ASSERT_TRUE(deps);
  ASSERT_EQ(1, deps->mtime);
  ASSERT_EQ(2, deps->node_count);
  ASSERT_EQ("foo.h", deps->nodes[0]->path());
  ASSERT_EQ("bar.h", deps->nodes[1]->path());
}

// Verify that adding the same deps twice doesn't grow the file.
TEST_F(DepsLogTest, DoubleEntry) {
  // Write some deps to the file and grab its size.
  int file_size;
  {
    State state;
    DepsLog log;
    string err;
    EXPECT_TRUE(log.OpenForWrite(kTestFilename, &err));
    ASSERT_EQ("", err);

    vector<Node*> deps;
    deps.push_back(state.GetNode("foo.h"));
    deps.push_back(state.GetNode("bar.h"));
    log.RecordDeps(state.GetNode("out.o"), 1, deps);
    log.Close();

    struct stat st;
    ASSERT_EQ(0, stat(kTestFilename, &st));
    file_size = (int)st.st_size;
    ASSERT_GT(file_size, 0);
  }

  // Now reload the file, and readd the same deps.
  {
    State state;
    DepsLog log;
    string err;
    EXPECT_TRUE(log.Load(kTestFilename, &state, &err));

    EXPECT_TRUE(log.OpenForWrite(kTestFilename, &err));
    ASSERT_EQ("", err);

    vector<Node*> deps;
    deps.push_back(state.GetNode("foo.h"));
    deps.push_back(state.GetNode("bar.h"));
    log.RecordDeps(state.GetNode("out.o"), 1, deps);
    log.Close();

    struct stat st;
    ASSERT_EQ(0, stat(kTestFilename, &st));
    int file_size_2 = (int)st.st_size;
    ASSERT_EQ(file_size, file_size_2);
  }
}

// Verify that adding the new deps works and can be compacted away.
TEST_F(DepsLogTest, Recompact) {
  // Write some deps to the file and grab its size.
  int file_size;
  {
    State state;
    DepsLog log;
    string err;
    ASSERT_TRUE(log.OpenForWrite(kTestFilename, &err));
    ASSERT_EQ("", err);

    vector<Node*> deps;
    deps.push_back(state.GetNode("foo.h"));
    deps.push_back(state.GetNode("bar.h"));
    log.RecordDeps(state.GetNode("out.o"), 1, deps);
    log.Close();

    struct stat st;
    ASSERT_EQ(0, stat(kTestFilename, &st));
    file_size = (int)st.st_size;
    ASSERT_GT(file_size, 0);
  }

  // Now reload the file, and add slighly different deps.
  int file_size_2;
  {
    State state;
    DepsLog log;
    string err;
    ASSERT_TRUE(log.Load(kTestFilename, &state, &err));

    ASSERT_TRUE(log.OpenForWrite(kTestFilename, &err));
    ASSERT_EQ("", err);

    vector<Node*> deps;
    deps.push_back(state.GetNode("foo.h"));
    log.RecordDeps(state.GetNode("out.o"), 1, deps);
    log.Close();

    struct stat st;
    ASSERT_EQ(0, stat(kTestFilename, &st));
    file_size_2 = (int)st.st_size;
    // The file should grow to record the new deps.
    ASSERT_GT(file_size_2, file_size);
  }

  // Now reload the file, verify the new deps have replaced the old, then
  // recompact.
  {
    State state;
    DepsLog log;
    string err;
    ASSERT_TRUE(log.Load(kTestFilename, &state, &err));

    DepsLog::Deps* deps = log.GetDeps(state.GetNode("out.o"));
    ASSERT_TRUE(deps);
    ASSERT_EQ(1, deps->mtime);
    ASSERT_EQ(1, deps->node_count);
    ASSERT_EQ("foo.h", deps->nodes[0]->path());

    ASSERT_TRUE(log.Recompact(kTestFilename, &err));

    struct stat st;
    ASSERT_EQ(0, stat(kTestFilename, &st));
    int file_size_3 = (int)st.st_size;
    // The file should have shrunk a bit for the smaller deps.
    ASSERT_LT(file_size_3, file_size_2);
  }
}

// Verify that invalid file headers cause a new build.
TEST_F(DepsLogTest, InvalidHeader) {
  const char *kInvalidHeaders[] = {
    "",                              // Emtpy file.
    "# ninjad",                      // Truncated first line.
    "# ninjadeps\n",                 // No version int.
    "# ninjadeps\n\001\002",         // Truncated version int.
    "# ninjadeps\n\001\002\003\004"  // Invalid version int.
  };
  for (size_t i = 0; i < sizeof(kInvalidHeaders) / sizeof(kInvalidHeaders[0]);
       ++i) {
    FILE* deps_log = fopen(kTestFilename, "wb");
    ASSERT_TRUE(deps_log != NULL);
    ASSERT_EQ(
        strlen(kInvalidHeaders[i]),
        fwrite(kInvalidHeaders[i], 1, strlen(kInvalidHeaders[i]), deps_log));
    ASSERT_EQ(0 ,fclose(deps_log));

    string err;
    DepsLog log;
    State state;
    ASSERT_TRUE(log.Load(kTestFilename, &state, &err));
    EXPECT_EQ("bad deps log signature or version; starting over", err);
  }
}

}  // anonymous namespace
