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

#include <gtest/gtest.h>

#include "interesting_paths.h"
#include "test.h"

#include <windows.h>

namespace {

class InterestingPathsTest : public testing::Test {
 public:
  virtual void SetUp() {
    // These tests do real disk accesses, so create a temp dir.
    temp_dir_.CreateAndEnter("Ninja-InterestingPathsTest");
  }

  virtual void TearDown() {
    temp_dir_.Cleanup();
  }

  ScopedTempDir temp_dir_;
};

TEST_F(InterestingPathsTest, CreateAndDirty) {
  InterestingPaths ips(true);
  int num_entries;
  DWORDLONG* entries;
  ips.StartLookups();
  EXPECT_EQ(false, ips.IsDirty(&num_entries, &entries));
  EXPECT_EQ(false, ips.IsPathInteresting(0));
  ips.FinishLookups();

  ips.StartAdditions();
  ips.Add("a");
  ips.Add("b");
  ips.FinishAdditions();

  ips.StartLookups();
  EXPECT_EQ(true, ips.IsDirty(&num_entries, &entries));
  EXPECT_EQ(1, num_entries); // Two files, both in root.
  ips.FinishLookups();

  _mkdir("x");
  ips.StartAdditions();
  ips.Add("x/y");
  ips.FinishAdditions();

  ips.StartLookups();
  EXPECT_EQ(true, ips.IsDirty(&num_entries, &entries));
  EXPECT_EQ(2, num_entries); // Three files, two roots.
  ips.FinishLookups();

  ips.StartLookups();
  EXPECT_EQ(true, ips.IsDirty(&num_entries, &entries));
  ips.ClearDirty();
  EXPECT_EQ(false, ips.IsDirty(&num_entries, &entries));
  ips.FinishLookups();

  _mkdir("a");
  _mkdir("d");
  _mkdir("f");
  _mkdir("f/g");
  ips.StartAdditions();
  ips.Add("a/b");
  ips.Add("d/e");
  ips.Add("f/g/h");
  ips.FinishAdditions();

  // Subdirs.
  ips.StartLookups();
  EXPECT_EQ(true, ips.IsDirty(&num_entries, &entries));
  EXPECT_EQ(5, num_entries); // ., x, a, d, g. Note that 'f' isn't added.
  ips.FinishLookups();
}


}  // namespace
