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

#include "disk_interface.h"
#include "interesting_paths.h"
#include "stat_cache.h"
#include "test.h"

#include <windows.h>

namespace {

class StatCacheTest : public testing::Test {
 public:
  virtual void SetUp() {
    // These tests do real disk accesses, so create a temp dir.
    temp_dir_.CreateAndEnter("Ninja-StatCacheTest");
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

TEST_F(StatCacheTest, PathDirtying) {
  RealDiskInterface disk_interface;
  StatCache cache(true, &disk_interface);

  cache.StartBuild();
  EXPECT_EQ(-1, cache.GetMtime("a"));
  vector<string> failed = cache.FinishBuild(true);

  EXPECT_EQ(1, failed.size());
  EXPECT_EQ("a", failed[0]);

  cache.StartProcessingChanges();

  cache.FinishProcessingChanges();
}


}  // namespace
