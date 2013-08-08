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

#include "msvc_helper.h"

#include <gtest/gtest.h>

#include "test.h"
#include "util.h"

TEST(CLParserTest, ShowIncludes) {
  ASSERT_EQ("", CLParser::FilterShowIncludes(""));

  ASSERT_EQ("", CLParser::FilterShowIncludes("Sample compiler output"));
  ASSERT_EQ("c:\\Some Files\\foobar.h",
            CLParser::FilterShowIncludes("Note: including file: "
                                         "c:\\Some Files\\foobar.h"));
  ASSERT_EQ("c:\\initspaces.h",
            CLParser::FilterShowIncludes("Note: including file:    "
                                         "c:\\initspaces.h"));
}

TEST(CLParserTest, FilterInputFilename) {
  ASSERT_TRUE(CLParser::FilterInputFilename("foobar.cc"));
  ASSERT_TRUE(CLParser::FilterInputFilename("foo bar.cc"));
  ASSERT_TRUE(CLParser::FilterInputFilename("baz.c"));
  ASSERT_TRUE(CLParser::FilterInputFilename("FOOBAR.CC"));

  ASSERT_FALSE(CLParser::FilterInputFilename(
                   "src\\cl_helper.cc(166) : fatal error C1075: end "
                   "of file found ..."));
}

TEST(CLParserTest, ParseSimple) {
  CLParser parser;
  string output = parser.Parse(
      "foo\r\n"
      "Note: including file:  foo.h\r\n"
      "bar\r\n");

  ASSERT_EQ("foo\nbar\n", output);
  ASSERT_EQ(1u, parser.includes_.size());
  ASSERT_EQ("foo.h", *parser.includes_.begin());
}

TEST(CLParserTest, ParseFilenameFilter) {
  CLParser parser;
  string output = parser.Parse(
      "foo.cc\r\n"
      "cl: warning\r\n");
  ASSERT_EQ("cl: warning\n", output);
}

TEST(CLParserTest, ParseSystemInclude) {
  CLParser parser;
  string output = parser.Parse(
      "Note: including file: c:\\Program Files\\foo.h\r\n"
      "Note: including file: d:\\Microsoft Visual Studio\\bar.h\r\n"
      "Note: including file: path.h\r\n");
  // We should have dropped the first two includes because they look like
  // system headers.
  ASSERT_EQ("", output);
  ASSERT_EQ(1u, parser.includes_.size());
  ASSERT_EQ("path.h", *parser.includes_.begin());
}

TEST(CLParserTest, DuplicatedHeader) {
  CLParser parser;
  string output = parser.Parse(
      "Note: including file: foo.h\r\n"
      "Note: including file: bar.h\r\n"
      "Note: including file: foo.h\r\n");
  // We should have dropped one copy of foo.h.
  ASSERT_EQ("", output);
  ASSERT_EQ(2u, parser.includes_.size());
}

TEST(CLParserTest, DuplicatedHeaderPathConverted) {
  CLParser parser;
  string output = parser.Parse(
      "Note: including file: sub/foo.h\r\n"
      "Note: including file: bar.h\r\n"
      "Note: including file: sub\\foo.h\r\n");
  // We should have dropped one copy of foo.h.
  ASSERT_EQ("", output);
  ASSERT_EQ(2u, parser.includes_.size());
}

TEST(CLParserTest, SpacesInFilename) {
  ASSERT_EQ("sub\\some\\ sdk\\foo.h",
            EscapeForDepfile("sub\\some sdk\\foo.h"));
}

TEST(MSVCHelperTest, EnvBlock) {
  char env_block[] = "foo=bar\0";
  CLWrapper cl;
  cl.SetEnvBlock(env_block);
  string output;
  cl.Run("cmd /c \"echo foo is %foo%", &output);
  ASSERT_EQ("foo is bar\r\n", output);
}
