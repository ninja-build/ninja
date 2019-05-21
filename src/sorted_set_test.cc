// Copyright 2019 Google Inc. All Rights Reserved.
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

#include "sorted_set.h"

#include "test.h"

TEST(SortedSet, Iterate) {
  SortedSet<unsigned, less<unsigned> > testee;
  testee.insert(1);
  testee.insert(2);
  for (SortedSet<unsigned, less<unsigned> >::iterator iter = testee.begin();
       iter != testee.end(); ++iter) {
    *iter = *iter + 2;
  }

  EXPECT_TRUE(testee.has_element(3));
  EXPECT_TRUE(testee.has_element(4));
  EXPECT_EQ(testee.size(), 2);
}

TEST(SortedSet, Drop) {
  SortedSet<unsigned, less<unsigned> > testee;
  testee.insert(1);
  testee.insert(2);
  unsigned u = testee.begin().drop();
  EXPECT_EQ(u, 1);
  EXPECT_EQ(testee.size(), 1);
}

TEST(SortedSet, Deduplication) {
  SortedSet<unsigned, less<unsigned> > testee;
  testee.insert(1);
  testee.insert(2);
  testee.insert(2);
  EXPECT_EQ(testee.size(), 2);
}

TEST(SortedSet, DescendingOrder) {
  SortedSet<unsigned, less<unsigned> > testee;
  testee.insert(1);
  testee.insert(2);
  testee.insert(0);
  testee.insert(4);
  for (size_t i = 1; i < testee.size(); ++i) {
    EXPECT_LT(testee[i-1], testee[i]);
  }
}
