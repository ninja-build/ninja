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

#include "dep_database.h"

#include <gtest/gtest.h>

#include "test.h"
#include "util.h"

class DepDatabaseTest : public testing::Test {
 public:
  virtual void SetUp() {
    // These tests do real disk accesses, so create a temp dir.
    temp_dir_.CreateAndEnter("Ninja-DepDatabase");
  }

  virtual void TearDown() {
    temp_dir_.Cleanup();
  }

  ScopedTempDir temp_dir_;
};

TEST_F(DepDatabaseTest, Empty) {
  DepDatabase db("depdb", true);
  ASSERT_EQ(0, db.FindDepData("nothing.c"));
}

TEST_F(DepDatabaseTest, AddAndRetrieve) {
  DepDatabase db("depdb", true);
  string data = "wee";
  db.InsertOrUpdateDepData("a.c", data.c_str(), data.size() + 1);
  const char* ret = db.FindDepData("a.c");
  ASSERT_NE(ret, (const char*)NULL);
  EXPECT_EQ(0, strcmp(ret, data.c_str()));
}

TEST_F(DepDatabaseTest, AddUpdateRetrieve) {
  DepDatabase db("depdb", true);
  string data = "wee";
  db.InsertOrUpdateDepData("a.c", data.c_str(), data.size() + 1);
  data = "blorp";
  db.InsertOrUpdateDepData("a.c", data.c_str(), data.size() + 1);
  const char* ret = db.FindDepData("a.c");
  ASSERT_NE(ret, (const char*)NULL);
  EXPECT_EQ(0, strcmp(ret, data.c_str()));
}

TEST_F(DepDatabaseTest, AddMultipleSorted) {
  DepDatabase db("depdb", true);
  string data;
  data = "wee";
  db.InsertOrUpdateDepData("a.c", data.c_str(), data.size() + 1);
  data = "waa";
  db.InsertOrUpdateDepData("b.c", data.c_str(), data.size() + 1);
  data = "woo";
  db.InsertOrUpdateDepData("x.c", data.c_str(), data.size() + 1);

  EXPECT_EQ(0, strcmp(db.FindDepData("a.c"), "wee"));
  EXPECT_EQ(0, strcmp(db.FindDepData("b.c"), "waa"));
  EXPECT_EQ(0, strcmp(db.FindDepData("x.c"), "woo"));
}

TEST_F(DepDatabaseTest, AddMultipleUnsorted) {
  DepDatabase db("depdb", true);
  string data;
  data = "woo";
  db.InsertOrUpdateDepData("x.c", data.c_str(), data.size() + 1);
  data = "waa";
  db.InsertOrUpdateDepData("b.c", data.c_str(), data.size() + 1);
  data = "wee";
  db.InsertOrUpdateDepData("a.c", data.c_str(), data.size() + 1);

  EXPECT_EQ(0, strcmp(db.FindDepData("a.c"), "wee"));
  EXPECT_EQ(0, strcmp(db.FindDepData("b.c"), "waa"));
  EXPECT_EQ(0, strcmp(db.FindDepData("x.c"), "woo"));
}
