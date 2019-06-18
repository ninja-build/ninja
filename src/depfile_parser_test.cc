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

#include "test.h"

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
  EXPECT_EQ("build/ninja.o", parser_.out_.AsString());
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
  EXPECT_EQ("foo.o", parser_.out_.AsString());
  EXPECT_EQ(2u, parser_.ins_.size());
}

TEST_F(DepfileParserTest, CarriageReturnContinuation) {
  string err;
  EXPECT_TRUE(Parse(
"foo.o: \\\r\n"
"  bar.h baz.h\r\n",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("foo.o", parser_.out_.AsString());
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
  EXPECT_EQ("Project\\Dir\\Build\\Release8\\Foo\\Foo.res",
            parser_.out_.AsString());
  EXPECT_EQ(4u, parser_.ins_.size());
}

TEST_F(DepfileParserTest, Spaces) {
  string err;
  EXPECT_TRUE(Parse(
"a\\ bc\\ def:   a\\ b c d",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("a bc def",
            parser_.out_.AsString());
  ASSERT_EQ(3u, parser_.ins_.size());
  EXPECT_EQ("a b",
            parser_.ins_[0].AsString());
  EXPECT_EQ("c",
            parser_.ins_[1].AsString());
  EXPECT_EQ("d",
            parser_.ins_[2].AsString());
}

TEST_F(DepfileParserTest, MultipleBackslashes) {
  // Successive 2N+1 backslashes followed by space (' ') are replaced by N >= 0
  // backslashes and the space. A single backslash before hash sign is removed.
  // Other backslashes remain untouched (including 2N backslashes followed by
  // space).
  string err;
  EXPECT_TRUE(Parse(
"a\\ b\\#c.h: \\\\\\\\\\  \\\\\\\\ \\\\share\\info\\\\#1",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("a b#c.h",
            parser_.out_.AsString());
  ASSERT_EQ(3u, parser_.ins_.size());
  EXPECT_EQ("\\\\ ",
            parser_.ins_[0].AsString());
  EXPECT_EQ("\\\\\\\\",
            parser_.ins_[1].AsString());
  EXPECT_EQ("\\\\share\\info\\#1",
            parser_.ins_[2].AsString());
}

TEST_F(DepfileParserTest, Escapes) {
  // Put backslashes before a variety of characters, see which ones make
  // it through.
  string err;
  EXPECT_TRUE(Parse(
"\\!\\@\\#$$\\%\\^\\&\\[\\]\\\\:",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("\\!\\@#$\\%\\^\\&\\[\\]\\\\",
            parser_.out_.AsString());
  ASSERT_EQ(0u, parser_.ins_.size());
}

TEST_F(DepfileParserTest, SpecialChars) {
  // See filenames like istreambuf.iterator_op!= in
  // https://github.com/google/libcxx/tree/master/test/iterators/stream.iterators/istreambuf.iterator/
  string err;
  EXPECT_TRUE(Parse(
"C:/Program\\ Files\\ (x86)/Microsoft\\ crtdefs.h: \\\n"
" en@quot.header~ t+t-x!=1 \\\n"
" openldap/slapd.d/cn=config/cn=schema/cn={0}core.ldif\\\n"
" Fu\303\244ball\\\n"
" a[1]b@2%c",
      &err));
  ASSERT_EQ("", err);
  EXPECT_EQ("C:/Program Files (x86)/Microsoft crtdefs.h",
            parser_.out_.AsString());
  ASSERT_EQ(5u, parser_.ins_.size());
  EXPECT_EQ("en@quot.header~",
            parser_.ins_[0].AsString());
  EXPECT_EQ("t+t-x!=1",
            parser_.ins_[1].AsString());
  EXPECT_EQ("openldap/slapd.d/cn=config/cn=schema/cn={0}core.ldif",
            parser_.ins_[2].AsString());
  EXPECT_EQ("Fu\303\244ball",
            parser_.ins_[3].AsString());
  EXPECT_EQ("a[1]b@2%c",
            parser_.ins_[4].AsString());
}

TEST_F(DepfileParserTest, UnifyMultipleOutputs) {
  // check that multiple duplicate targets are properly unified
  string err;
  EXPECT_TRUE(Parse("foo foo: x y z", &err));
  ASSERT_EQ("foo", parser_.out_.AsString());
  ASSERT_EQ(3u, parser_.ins_.size());
  EXPECT_EQ("x", parser_.ins_[0].AsString());
  EXPECT_EQ("y", parser_.ins_[1].AsString());
  EXPECT_EQ("z", parser_.ins_[2].AsString());
}

TEST_F(DepfileParserTest, RejectMultipleDifferentOutputs) {
  // check that multiple different outputs are rejected by the parser
  string err;
  EXPECT_FALSE(Parse("foo bar: x y z", &err));
  ASSERT_EQ("depfile has multiple output paths", err);
}

TEST_F(DepfileParserTest, MultipleEmptyRules) {
  string err;
  EXPECT_TRUE(Parse("foo: x\n"
                    "foo: \n"
                    "foo:\n", &err));
  ASSERT_EQ("foo", parser_.out_.AsString());
  ASSERT_EQ(1u, parser_.ins_.size());
  EXPECT_EQ("x", parser_.ins_[0].AsString());
}

TEST_F(DepfileParserTest, UnifyMultipleRulesLF) {
  string err;
  EXPECT_TRUE(Parse("foo: x\n"
                    "foo: y\n"
                    "foo \\\n"
                    "foo: z\n", &err));
  ASSERT_EQ("foo", parser_.out_.AsString());
  ASSERT_EQ(3u, parser_.ins_.size());
  EXPECT_EQ("x", parser_.ins_[0].AsString());
  EXPECT_EQ("y", parser_.ins_[1].AsString());
  EXPECT_EQ("z", parser_.ins_[2].AsString());
}

TEST_F(DepfileParserTest, UnifyMultipleRulesCRLF) {
  string err;
  EXPECT_TRUE(Parse("foo: x\r\n"
                    "foo: y\r\n"
                    "foo \\\r\n"
                    "foo: z\r\n", &err));
  ASSERT_EQ("foo", parser_.out_.AsString());
  ASSERT_EQ(3u, parser_.ins_.size());
  EXPECT_EQ("x", parser_.ins_[0].AsString());
  EXPECT_EQ("y", parser_.ins_[1].AsString());
  EXPECT_EQ("z", parser_.ins_[2].AsString());
}

TEST_F(DepfileParserTest, UnifyMixedRulesLF) {
  string err;
  EXPECT_TRUE(Parse("foo: x\\\n"
                    "     y\n"
                    "foo \\\n"
                    "foo: z\n", &err));
  ASSERT_EQ("foo", parser_.out_.AsString());
  ASSERT_EQ(3u, parser_.ins_.size());
  EXPECT_EQ("x", parser_.ins_[0].AsString());
  EXPECT_EQ("y", parser_.ins_[1].AsString());
  EXPECT_EQ("z", parser_.ins_[2].AsString());
}

TEST_F(DepfileParserTest, UnifyMixedRulesCRLF) {
  string err;
  EXPECT_TRUE(Parse("foo: x\\\r\n"
                    "     y\r\n"
                    "foo \\\r\n"
                    "foo: z\r\n", &err));
  ASSERT_EQ("foo", parser_.out_.AsString());
  ASSERT_EQ(3u, parser_.ins_.size());
  EXPECT_EQ("x", parser_.ins_[0].AsString());
  EXPECT_EQ("y", parser_.ins_[1].AsString());
  EXPECT_EQ("z", parser_.ins_[2].AsString());
}

TEST_F(DepfileParserTest, IndentedRulesLF) {
  string err;
  EXPECT_TRUE(Parse(" foo: x\n"
                    " foo: y\n"
                    " foo: z\n", &err));
  ASSERT_EQ("foo", parser_.out_.AsString());
  ASSERT_EQ(3u, parser_.ins_.size());
  EXPECT_EQ("x", parser_.ins_[0].AsString());
  EXPECT_EQ("y", parser_.ins_[1].AsString());
  EXPECT_EQ("z", parser_.ins_[2].AsString());
}

TEST_F(DepfileParserTest, IndentedRulesCRLF) {
  string err;
  EXPECT_TRUE(Parse(" foo: x\r\n"
                    " foo: y\r\n"
                    " foo: z\r\n", &err));
  ASSERT_EQ("foo", parser_.out_.AsString());
  ASSERT_EQ(3u, parser_.ins_.size());
  EXPECT_EQ("x", parser_.ins_[0].AsString());
  EXPECT_EQ("y", parser_.ins_[1].AsString());
  EXPECT_EQ("z", parser_.ins_[2].AsString());
}

TEST_F(DepfileParserTest, TolerateMP) {
  string err;
  EXPECT_TRUE(Parse("foo: x y z\n"
                    "x:\n"
                    "y:\n"
                    "z:\n", &err));
  ASSERT_EQ("foo", parser_.out_.AsString());
  ASSERT_EQ(3u, parser_.ins_.size());
  EXPECT_EQ("x", parser_.ins_[0].AsString());
  EXPECT_EQ("y", parser_.ins_[1].AsString());
  EXPECT_EQ("z", parser_.ins_[2].AsString());
}

TEST_F(DepfileParserTest, MultipleRulesTolerateMP) {
  string err;
  EXPECT_TRUE(Parse("foo: x\n"
                    "x:\n"
                    "foo: y\n"
                    "y:\n"
                    "foo: z\n"
                    "z:\n", &err));
  ASSERT_EQ("foo", parser_.out_.AsString());
  ASSERT_EQ(3u, parser_.ins_.size());
  EXPECT_EQ("x", parser_.ins_[0].AsString());
  EXPECT_EQ("y", parser_.ins_[1].AsString());
  EXPECT_EQ("z", parser_.ins_[2].AsString());
}

TEST_F(DepfileParserTest, MultipleRulesRejectDifferentOutputs) {
  // check that multiple different outputs are rejected by the parser
  // when spread across multiple rules
  DepfileParserOptions parser_opts;
  parser_opts.depfile_distinct_target_lines_action_ =
      kDepfileDistinctTargetLinesActionError;
  DepfileParser parser(parser_opts);
  string err;
  string input =
      "foo: x y\n"
      "bar: y z\n";
  EXPECT_FALSE(parser.Parse(&input, &err));
  ASSERT_EQ("depfile has multiple output paths (on separate lines)"
            " [-w depfilemulti=err]", err);
}
