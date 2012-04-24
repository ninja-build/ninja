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

#include "change_journal.h"
#include "test.h"

#include <windows.h>

namespace {

class ChangeJournalTest : public testing::Test {
 public:
  virtual void SetUp() {
    // These tests do real disk accesses, so create a temp dir.
    temp_dir_.CreateAndEnter("Ninja-ChangeJournalTest");
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
};

TEST_F(ChangeJournalTest, Create) {
  InterestingPaths interesting_paths(true);
  StatCache cache(true, interesting_paths);
  ChangeJournal cj('C', cache);
  EXPECT_EQ(cj.DriveLetter(), "C");
  EXPECT_EQ(cj.DriveLetterChar(), 'C');

  cache.StartBuild();
  EXPECT_EQ(cache.GetMtime("a"), -1);
  cache.FinishBuild();

  cj.CheckForDirtyPaths();

  cache.StartBuild();
  EXPECT_EQ(cache.GetMtime("a"), -1);
  cache.FinishBuild();
}


}  // namespace
