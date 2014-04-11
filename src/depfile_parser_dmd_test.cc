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

#include "depfile_parser_dmd.h"

#include <gtest/gtest.h>

struct DepfileParserDMDTest : public testing::Test {
  bool Parse(const char* input, string* err);

  DepfileParserDMD parser_;
  string input_;
};

bool DepfileParserDMDTest::Parse(const char* input, string* err) {
  input_ = input;
  return parser_.Parse(&input_, err);
}

TEST_F(DepfileParserDMDTest, Basic) {
  string err;
  EXPECT_TRUE(Parse(
"std.path (/usr/include/d/std/path.d) : private : object (/usr/include/d/ldc/object.di)\n"
"std.path (/usr/include/d/std/path.d) : private : std.algorithm (/usr/include/d/std/algorithm.d)\n"
"std.path (/usr/include/d/std/path.d) : private : std.array (/usr/include/d/std/array.d)\n"
"std.path (/usr/include/d/std/path.d) : private : std.conv (/usr/include/d/std/conv.d)\n"
"std.path (/usr/include/d/std/path.d) : private : std.file (/usr/include/d/std/file.d):getcwd\n"
"std.path (/usr/include/d/std/path.d) : private : std.string (/usr/include/d/std/string.d)\n"
"std.path (/usr/include/d/std/path.d) : private : std.traits (/usr/include/d/std/traits.d)\n"
"std.path (/usr/include/d/std/path.d) : private : core.exception (/usr/include/d/core/exception.d)\n"
"std.path (/usr/include/d/std/path.d) : private : core.stdc.errno (/usr/include/d/core/stdc/errno.d)\n"
"std.path (/usr/include/d/std/path.d) : private : core.sys.posix.pwd (/usr/include/d/core/sys/posix/pwd.d)\n"
"std.path (/usr/include/d/std/path.d) : private : core.sys.posix.stdlib (/usr/include/d/core/sys/posix/stdlib.d)\n",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("/usr/include/d/std/path.d", parser_.out_.AsString());
  EXPECT_EQ(11, parser_.ins_.size());
}

TEST_F(DepfileParserDMDTest, BasicWithBindlist) {
  string err;
  EXPECT_TRUE(Parse(
"std.path (/usr/include/d/std/path.d) : private : object (/usr/include/d/ldc/object.di)\n"
"std.path (/usr/include/d/std/path.d) : private : core.exception (/usr/include/d/core/exception.d):onOutOfMemoryError\n",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("/usr/include/d/std/path.d", parser_.out_.AsString());
  EXPECT_EQ(2, parser_.ins_.size());
}

TEST_F(DepfileParserDMDTest, BasicWithDuplicates) {
  string err;
  EXPECT_TRUE(Parse(
"std.path (/usr/include/d/std/path.d) : private : object (/usr/include/d/ldc/object.di)\n"
"std.path (/usr/include/d/std/path.d) : private : object (/usr/include/d/ldc/object.di)\n",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("/usr/include/d/std/path.d", parser_.out_.AsString());
  EXPECT_EQ(1, parser_.ins_.size());
}

TEST_F(DepfileParserDMDTest, Escapes) {
  string err;
  EXPECT_TRUE(Parse(
"std.path (/usr/include/d/std/path.d) : private : module.with.escapes (/path/with\\\\/\\silly\\)/chars\\(/module/with/escapes.d)\n",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("/usr/include/d/std/path.d", parser_.out_.AsString());
  EXPECT_EQ(1u, parser_.ins_.size());
  EXPECT_EQ("/path/with\\/\\silly)/chars(/module/with/escapes.d",
            parser_.ins_[0].AsString());
}

TEST_F(DepfileParserDMDTest, Spaces) {
  string err;
  EXPECT_TRUE(Parse(
"std.path (/usr/include/d/std/path.d) : private : module.with.escapes (/path/with/spaces/module/with/escapes.d)\n",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("/usr/include/d/std/path.d", parser_.out_.AsString());
  ASSERT_EQ(1u, parser_.ins_.size());
  EXPECT_EQ("/path/with/spaces/module/with/escapes.d",
            parser_.ins_[0].AsString());
}
