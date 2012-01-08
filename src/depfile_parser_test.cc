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
  StringPiece target_;
  vector<StringPiece> inputs_;
};

bool DepfileParserTest::Parse(const char* input, string* err) {
  input_ = input;
  return parser_.Parse(&input_, &target_, &inputs_, err);
}

TEST_F(DepfileParserTest, Basic) {
  string err;
  EXPECT_TRUE(Parse(
"build/ninja.o: ninja.cc ninja.h eval_env.h manifest_parser.h\n",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("build/ninja.o", target_.AsString());
  EXPECT_EQ(4u, inputs_.size());
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
  EXPECT_EQ("foo.o", target_.AsString());
  EXPECT_EQ(2u, inputs_.size());
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
  EXPECT_EQ("Project\\Dir\\Build\\Release8\\Foo\\Foo.res",
            target_.AsString());
  EXPECT_EQ(4u, inputs_.size());
}

TEST_F(DepfileParserTest, Spaces) {
  string err;
  EXPECT_TRUE(Parse(
"a\\ bc\\ def:   a\\ b c d",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("a bc def",
            target_.AsString());
  ASSERT_EQ(3u, inputs_.size());
  EXPECT_EQ("a b",
            inputs_[0].AsString());
  EXPECT_EQ("c",
            inputs_[1].AsString());
  EXPECT_EQ("d",
            inputs_[2].AsString());
}

TEST_F(DepfileParserTest, Escapes) {
  // Put backslashes before a variety of characters, see which ones make
  // it through.
  string err;
  EXPECT_TRUE(Parse(
"\\!\\@\\#\\$\\%\\^\\&\\\\",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("\\!\\@#$\\%\\^\\&\\",
            target_.AsString());
  ASSERT_EQ(0u, inputs_.size());
}
