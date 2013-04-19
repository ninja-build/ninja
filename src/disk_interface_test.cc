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

#include <gtest/gtest.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

#include "disk_interface.h"
#include "graph.h"
#include "test.h"

namespace {

struct DiskInterfaceTest : public testing::Test {
  virtual void SetUp() {
    // These tests do real disk accesses, so create a temp dir.
    temp_dir_.CreateAndEnter("Ninja-DiskInterfaceTest");
  }

  virtual void TearDown() {
    temp_dir_.Cleanup();
  }

  bool Touch(const char* path) {
    FILE *f = fopen(path, "w");
    if (!f)
      return false;
    return fclose(f) == 0;
  }

  ScopedTempDir temp_dir_;
  RealDiskInterface disk_;
};

TEST_F(DiskInterfaceTest, StatMissingFile) {
  EXPECT_EQ(0, disk_.Stat("nosuchfile"));

  // On Windows, the errno for a file in a nonexistent directory
  // is different.
  EXPECT_EQ(0, disk_.Stat("nosuchdir/nosuchfile"));

  // On POSIX systems, the errno is different if a component of the
  // path prefix is not a directory.
  ASSERT_TRUE(Touch("notadir"));
  EXPECT_EQ(0, disk_.Stat("notadir/nosuchfile"));
}

TEST_F(DiskInterfaceTest, StatBadPath) {
  disk_.quiet_ = true;
#ifdef _WIN32
  string bad_path("cc:\\foo");
  EXPECT_EQ(-1, disk_.Stat(bad_path));
#else
  string too_long_name(512, 'x');
  EXPECT_EQ(-1, disk_.Stat(too_long_name));
#endif
  disk_.quiet_ = false;
}

TEST_F(DiskInterfaceTest, StatExistingFile) {
  ASSERT_TRUE(Touch("file"));
  EXPECT_GT(disk_.Stat("file"), 1);
}

TEST_F(DiskInterfaceTest, ReadFile) {
  string err;
  EXPECT_EQ("", disk_.ReadFile("foobar", &err));
  EXPECT_EQ("", err);

  const char* kTestFile = "testfile";
  FILE* f = fopen(kTestFile, "wb");
  ASSERT_TRUE(f);
  const char* kTestContent = "test content\nok";
  fprintf(f, "%s", kTestContent);
  ASSERT_EQ(0, fclose(f));

  EXPECT_EQ(kTestContent, disk_.ReadFile(kTestFile, &err));
  EXPECT_EQ("", err);
}

TEST_F(DiskInterfaceTest, MakeDirs) {
  EXPECT_TRUE(disk_.MakeDirs("path/with/double//slash/"));
}

TEST_F(DiskInterfaceTest, RemoveFile) {
  const char* kFileName = "file-to-remove";
  ASSERT_TRUE(Touch(kFileName));
  EXPECT_EQ(0, disk_.RemoveFile(kFileName));
  EXPECT_EQ(1, disk_.RemoveFile(kFileName));
  EXPECT_EQ(1, disk_.RemoveFile("does not exist"));
}

struct StatTest : public StateTestWithBuiltinRules,
                  public DiskInterface {
  StatTest() : scan_(&state_, NULL, NULL, this) {}

  // DiskInterface implementation.
  virtual TimeStamp Stat(const string& path);
  virtual bool WriteFile(const string& path, const string& contents) {
    assert(false);
    return true;
  }
  virtual bool MakeDir(const string& path) {
    assert(false);
    return false;
  }
  virtual string ReadFile(const string& path, string* err) {
    assert(false);
    return "";
  }
  virtual int RemoveFile(const string& path) {
    assert(false);
    return 0;
  }

  DependencyScan scan_;
  map<string, TimeStamp> mtimes_;
  vector<string> stats_;
};

TimeStamp StatTest::Stat(const string& path) {
  stats_.push_back(path);
  map<string, TimeStamp>::iterator i = mtimes_.find(path);
  if (i == mtimes_.end())
    return 0;  // File not found.
  return i->second;
}

TEST_F(StatTest, Simple) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat in\n"));

  Node* out = GetNode("out");
  out->Stat(this);
  ASSERT_EQ(1u, stats_.size());
  scan_.RecomputeDirty(out->in_edge(), NULL);
  ASSERT_EQ(2u, stats_.size());
  ASSERT_EQ("out", stats_[0]);
  ASSERT_EQ("in",  stats_[1]);
}

TEST_F(StatTest, TwoStep) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n"));

  Node* out = GetNode("out");
  out->Stat(this);
  ASSERT_EQ(1u, stats_.size());
  scan_.RecomputeDirty(out->in_edge(), NULL);
  ASSERT_EQ(3u, stats_.size());
  ASSERT_EQ("out", stats_[0]);
  ASSERT_TRUE(GetNode("out")->dirty());
  ASSERT_EQ("mid",  stats_[1]);
  ASSERT_TRUE(GetNode("mid")->dirty());
  ASSERT_EQ("in",  stats_[2]);
}

TEST_F(StatTest, Tree) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat mid1 mid2\n"
"build mid1: cat in11 in12\n"
"build mid2: cat in21 in22\n"));

  Node* out = GetNode("out");
  out->Stat(this);
  ASSERT_EQ(1u, stats_.size());
  scan_.RecomputeDirty(out->in_edge(), NULL);
  ASSERT_EQ(1u + 6u, stats_.size());
  ASSERT_EQ("mid1", stats_[1]);
  ASSERT_TRUE(GetNode("mid1")->dirty());
  ASSERT_EQ("in11", stats_[2]);
}

TEST_F(StatTest, Middle) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n"));

  mtimes_["in"] = 1;
  mtimes_["mid"] = 0;  // missing
  mtimes_["out"] = 1;

  Node* out = GetNode("out");
  out->Stat(this);
  ASSERT_EQ(1u, stats_.size());
  scan_.RecomputeDirty(out->in_edge(), NULL);
  ASSERT_FALSE(GetNode("in")->dirty());
  ASSERT_TRUE(GetNode("mid")->dirty());
  ASSERT_TRUE(GetNode("out")->dirty());
}

}  // namespace
