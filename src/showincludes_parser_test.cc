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

#include "showincludes_parser.h"

#include <gtest/gtest.h>

#include "test.h"
#include "util.h"

TEST(ShowIncludesTest, Empty) {
  vector<StringPiece> entries;
  string out = ShowIncludes::Filter("", &entries);
  ASSERT_EQ("", out);
  ASSERT_EQ(0u, entries.size());
}

TEST(ShowIncludesTest, Simple) {
  vector<StringPiece> entries;
  string out = ShowIncludes::Filter(
"Sample compiler output\r\n"
"Note: including file: c:\\Program Files\\foobar.h\r\n"
"another text line\r\n"
"Note: including file:   c:\\initspaces.h\r\n",
&entries);

  EXPECT_EQ(
"Sample compiler output\r\n"
"another text line\r\n",
out);
  ASSERT_EQ(2u, entries.size());
  EXPECT_EQ("c:\\Program Files\\foobar.h", entries[0].AsString());
  EXPECT_EQ("c:\\initspaces.h", entries[1].AsString());
}
