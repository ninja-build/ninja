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

#include "deplist.h"

#include <gtest/gtest.h>

#include "test.h"
#include "util.h"

class DeplistTest : public testing::Test {
 public:
  virtual void SetUp() {
    // These tests do real disk accesses, so create a temp dir.
    temp_dir_.CreateAndEnter("Ninja-DiskInterfaceTest");
  }

  virtual void TearDown() {
    temp_dir_.Cleanup();
  }

  ScopedTempDir temp_dir_;
};

TEST_F(DeplistTest, Empty) {
  vector<StringPiece> entries;
  string err;
  EXPECT_FALSE(Deplist::Load("", &entries, &err));
  ASSERT_EQ("unexpected EOF", err);
}

TEST_F(DeplistTest, WriteRead) {
  vector<StringPiece> entries;
  entries.push_back("foo");
  entries.push_back("bar baz");
  entries.push_back("a");
  entries.push_back("b");

  const char* filename = "deplist";
  FILE* f = fopen(filename, "wb");
  ASSERT_TRUE(f != NULL);
  ASSERT_TRUE(Deplist::Write(f, entries));
  ASSERT_EQ(0, fclose(f));

  string contents;
  string err;
  ASSERT_EQ(0, ::ReadFile(filename, &contents, &err));
  vector<StringPiece> entries2;
  EXPECT_TRUE(Deplist::Load(contents, &entries2, &err));
  ASSERT_EQ("", err);

  ASSERT_EQ(entries.size(), entries2.size());
  for (int i = 0; i < (int)entries.size(); ++i)
    EXPECT_EQ(entries[i].AsString(), entries2[i].AsString());
}
