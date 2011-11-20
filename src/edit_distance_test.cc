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

#include "edit_distance.h"

#include "test.h"

TEST(EditDistanceTest, TestEmpty) {
  EXPECT_EQ(5, EditDistance("", "ninja"));
  EXPECT_EQ(5, EditDistance("ninja", ""));
  EXPECT_EQ(0, EditDistance("", ""));
}

TEST(EditDistanceTest, TestMaxDistance) {
  const bool allow_replacements = true;
  for (int max_distance = 1; max_distance < 7; ++max_distance) {
    EXPECT_EQ(max_distance + 1,
              EditDistance("abcdefghijklmnop", "ponmlkjihgfedcba",
                           allow_replacements, max_distance));
  }
}

TEST(EditDistanceTest, TestAllowReplacements) {
  bool allow_replacements = true;
  EXPECT_EQ(1, EditDistance("ninja", "njnja", allow_replacements));
  EXPECT_EQ(1, EditDistance("njnja", "ninja", allow_replacements));

  allow_replacements = false;
  EXPECT_EQ(2, EditDistance("ninja", "njnja", allow_replacements));
  EXPECT_EQ(2, EditDistance("njnja", "ninja", allow_replacements));
}

TEST(EditDistanceTest, TestBasics) {
  EXPECT_EQ(0, EditDistance("browser_tests", "browser_tests"));
  EXPECT_EQ(1, EditDistance("browser_test", "browser_tests"));
  EXPECT_EQ(1, EditDistance("browser_tests", "browser_test"));
}
