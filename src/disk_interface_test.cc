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

using namespace std;

namespace {

#ifdef _WIN32
#ifndef _mktemp_s
/// mingw has no mktemp.  Implement one with the same type as the one
/// found in the Windows API.
int _mktemp_s(char* templ) {
  char* ofs = strchr(templ, 'X');
  sprintf(ofs, "%d", rand() % 1000000);
  return 0;
}
#endif

/// Windows has no mkdtemp.  Implement it in terms of _mktemp_s.
char* mkdtemp(char* name_template) {
  int err = _mktemp_s(name_template);
  if (err < 0) {
    perror("_mktemp_s");
    return NULL;
  }

  err = _mkdir(name_template);
  if (err < 0) {
    perror("mkdir");
    return NULL;
  }

  return name_template;
}
#endif

class DiskInterfaceTest : public testing::Test {
 public:
  virtual void SetUp() {
    // Because we do real disk accesses, we create a temp dir within
    // the system temporary directory.

    // First change into the system temp dir and save it for cleanup.
    start_dir_ = GetSystemTempDir();
    ASSERT_EQ(0, chdir(start_dir_.c_str()));

    // Then create and change into a temporary subdirectory of that.
    temp_dir_name_ = MakeTempDir();
    ASSERT_FALSE(temp_dir_name_.empty());
    ASSERT_EQ(0, chdir(temp_dir_name_.c_str()));
  }

  virtual void TearDown() {
    // Move out of the directory we're about to clobber.
    ASSERT_EQ(0, chdir(start_dir_.c_str()));
#ifdef _WIN32
    ASSERT_EQ(0, system(("rmdir /s /q " + temp_dir_name_).c_str()));
#else
    ASSERT_EQ(0, system(("rm -rf " + temp_dir_name_).c_str()));
#endif
  }

  string GetSystemTempDir() {
#ifdef _WIN32
    char buf[1024];
    if (!GetTempPath(sizeof(buf), buf))
      return "";
    return buf;
#else
    const char* tempdir = getenv("TMPDIR");
    if (tempdir)
      return tempdir;
    return "/tmp";
#endif
  }

  string MakeTempDir() {
    char name_template[] = "DiskInterfaceTest-XXXXXX";
    char* name = mkdtemp(name_template);
    return name ? name : "";
  }

  string start_dir_;
  string temp_dir_name_;
  RealDiskInterface disk_;
};

TEST_F(DiskInterfaceTest, Stat) {
  EXPECT_EQ(0, disk_.Stat("nosuchfile"));

#ifdef _WIN32
  // TODO: find something that stat fails on for Windows.
#else
  string too_long_name(512, 'x');
  EXPECT_EQ(-1, disk_.Stat(too_long_name));
#endif

#ifdef _WIN32
  ASSERT_EQ(0, system("cmd.exe /c echo hi > file"));
#else
  ASSERT_EQ(0, system("touch file"));
#endif
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
#ifdef _WIN32
  string cmd = "cmd /c echo hi > ";
#else
  string cmd = "touch ";
#endif
  cmd += kFileName;
  ASSERT_EQ(0, system(cmd.c_str()));
  EXPECT_EQ(0, disk_.RemoveFile(kFileName));
  EXPECT_EQ(1, disk_.RemoveFile(kFileName));
  EXPECT_EQ(1, disk_.RemoveFile("does not exist"));
}

struct StatTest : public StateTestWithBuiltinRules,
                  public DiskInterface {
  // DiskInterface implementation.
  virtual int Stat(const string& path);
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

  map<string, time_t> mtimes_;
  vector<string> stats_;
};

int StatTest::Stat(const string& path) {
  stats_.push_back(path);
  map<string, time_t>::iterator i = mtimes_.find(path);
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
  Edge* edge = out->in_edge_;
  edge->RecomputeDirty(NULL, this, NULL);
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
  Edge* edge = out->in_edge_;
  edge->RecomputeDirty(NULL, this, NULL);
  ASSERT_EQ(3u, stats_.size());
  ASSERT_EQ("out", stats_[0]);
  ASSERT_TRUE(GetNode("out")->dirty_);
  ASSERT_EQ("mid",  stats_[1]);
  ASSERT_TRUE(GetNode("mid")->dirty_);
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
  Edge* edge = out->in_edge_;
  edge->RecomputeDirty(NULL, this, NULL);
  ASSERT_EQ(1u + 6u, stats_.size());
  ASSERT_EQ("mid1", stats_[1]);
  ASSERT_TRUE(GetNode("mid1")->dirty_);
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
  Edge* edge = out->in_edge_;
  edge->RecomputeDirty(NULL, this, NULL);
  ASSERT_FALSE(GetNode("in")->dirty_);
  ASSERT_TRUE(GetNode("mid")->dirty_);
  ASSERT_TRUE(GetNode("out")->dirty_);
}

}  // namespace
