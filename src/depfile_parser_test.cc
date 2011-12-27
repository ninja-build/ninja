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

TEST(DepfileParser, Basic) {
  DepfileParser parser;
  string err;
  EXPECT_TRUE(parser.Parse(
"build/ninja.o: ninja.cc ninja.h eval_env.h manifest_parser.h\n",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("build/ninja.o", parser.out_);
  EXPECT_EQ(4u, parser.ins_.size());
}

TEST(DepfileParser, EarlyNewlineAndWhitespace) {
  DepfileParser parser;
  string err;
  EXPECT_TRUE(parser.Parse(
" \\\n"
"  out: in\n",
      &err));
  ASSERT_EQ("", err);
}

TEST(DepfileParser, Continuation) {
  DepfileParser parser;
  string err;
  EXPECT_TRUE(parser.Parse(
"foo.o: \\\n"
"  bar.h baz.h\n",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("foo.o", parser.out_);
  EXPECT_EQ(2u, parser.ins_.size());
}

TEST(DepfileParser, BackSlashes) {
  DepfileParser parser;
  string err;
  EXPECT_TRUE(parser.Parse(
"Project\\Dir\\Build\\Release8\\Foo\\Foo.res : \\\n"
"  Dir\\Library\\Foo.rc \\\n"
"  Dir\\Library\\Version\\Bar.h \\\n"
"  Dir\\Library\\Foo.ico \\\n"
"  Project\\Thing\\Bar.tlb \\\n",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("Project\\Dir\\Build\\Release8\\Foo\\Foo.res",
            parser.out_);
  EXPECT_EQ(4u, parser.ins_.size());
}

TEST(DepfileParser, Spaces) {
  DepfileParser parser;
  string err;
  EXPECT_TRUE(parser.Parse(
"build/ninja\\ test.o: ninja\\ test.cc ninja.h\n",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("build/ninja test.o", parser.out_);
  EXPECT_EQ(2u, parser.ins_.size());
  EXPECT_EQ("ninja test.cc", parser.ins_.front());
  EXPECT_EQ("ninja.h", parser.ins_.back());
}
