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

#include "depfile_parser.h"

#include <gtest/gtest.h>

struct DepfileParserTest : public testing::Test {
  bool Parse(const char* input, string* err);

  DepfileParser parser_;
  string input_;
};

bool DepfileParserTest::Parse(const char* input, string* err) {
  input_ = input;
  return parser_.Parse(&input_, err);
}

TEST_F(DepfileParserTest, Basic) {
  string err;
  EXPECT_TRUE(Parse(
"build/ninja.o: ninja.cc ninja.h eval_env.h manifest_parser.h\n",
      &err));
  ASSERT_EQ("", err);
  ASSERT_EQ(parser_.outs_.size(), 1);
  EXPECT_EQ("build/ninja.o", parser_.outs_.front().AsString());
  EXPECT_EQ(4u, parser_.ins_.size());
}

TEST_F(DepfileParserTest, EarlyNewlineAndWhitespace) {
  string err;
  EXPECT_TRUE(Parse(
" \\\n"
"  out: in\n",
      &err));
  ASSERT_EQ("", err);
}

TEST_F(DepfileParserTest, Continuation) {
  string err;
  EXPECT_TRUE(Parse(
"foo.o: \\\n"
"  bar.h baz.h\n",
      &err));
  ASSERT_EQ("", err);
  ASSERT_EQ(parser_.outs_.size(), 1);
  EXPECT_EQ("foo.o", parser_.outs_.front().AsString());
  EXPECT_EQ(2u, parser_.ins_.size());
}

TEST_F(DepfileParserTest, BackSlashes) {
  string err;
  EXPECT_TRUE(Parse(
"Project\\Dir\\Build\\Release8\\Foo\\Foo.res : \\\n"
"  Dir\\Library\\Foo.rc \\\n"
"  Dir\\Library\\Version\\Bar.h \\\n"
"  Dir\\Library\\Foo.ico \\\n"
"  Project\\Thing\\Bar.tlb \\\n",
      &err));
  ASSERT_EQ("", err);
  ASSERT_EQ(parser_.outs_.size(), 1);
  EXPECT_EQ("Project\\Dir\\Build\\Release8\\Foo\\Foo.res",
            parser_.outs_.front().AsString());
  EXPECT_EQ(4u, parser_.ins_.size());
}

TEST_F(DepfileParserTest, Spaces) {
  string err;
  EXPECT_TRUE(Parse(
"a\\ bc\\ def:   a\\ b c d",
      &err));
  ASSERT_EQ("", err);
  ASSERT_EQ(parser_.outs_.size(), 1);
  EXPECT_EQ("a bc def",
            parser_.outs_.front().AsString());
  ASSERT_EQ(3u, parser_.ins_.size());
  EXPECT_EQ("a b",
            parser_.ins_[0].AsString());
  EXPECT_EQ("c",
            parser_.ins_[1].AsString());
  EXPECT_EQ("d",
            parser_.ins_[2].AsString());
}

TEST_F(DepfileParserTest, Escapes) {
  // Put backslashes before a variety of characters, see which ones make
  // it through.
  string err;
  EXPECT_TRUE(Parse(
"\\!\\@\\#\\$\\%\\^\\&\\\\",
      &err));
  ASSERT_EQ("", err);
  ASSERT_EQ(parser_.outs_.size(), 1);
  EXPECT_EQ("\\!\\@#$\\%\\^\\&\\",
            parser_.outs_.front().AsString());
  ASSERT_EQ(0u, parser_.ins_.size());
}

TEST_F(DepfileParserTest, MultipleTargets) {
  // check that multiple targets are parsed correctly
  string err;
  EXPECT_TRUE(Parse("foo bar: x y z", &err));
  ASSERT_EQ(parser_.outs_.size(), 2);
  EXPECT_EQ("foo", parser_.outs_[0].AsString());
  EXPECT_EQ("bar", parser_.outs_[1].AsString());
  ASSERT_EQ(parser_.ins_.size(), 3);
  EXPECT_EQ("x", parser_.ins_[0].AsString());
  EXPECT_EQ("y", parser_.ins_[1].AsString());
  EXPECT_EQ("z", parser_.ins_[2].AsString());
}
