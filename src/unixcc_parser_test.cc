// Copyright 2016 SAP SE All Rights Reserved.
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

#include "unixcc_parser.h"

#include "test.h"

struct UnixCCParserTest : public testing::Test {
  void Parse(const string& input);

  UnixCCParser parser_;
  string filtered_output_;
};

void UnixCCParserTest::Parse(const string& output) {
  parser_.Parse(output, &filtered_output_);
}

TEST_F(UnixCCParserTest, Empty) {
  Parse("");
  EXPECT_TRUE(filtered_output_.empty());
  EXPECT_TRUE(parser_.includes_.empty());
}

TEST_F(UnixCCParserTest, Basic) {
  Parse("src/unixcc_parser.h\n");
  EXPECT_TRUE(filtered_output_.empty());
  ASSERT_EQ(1u, parser_.includes_.size());
  EXPECT_EQ("src/unixcc_parser.h", *parser_.includes_.begin());
}

TEST_F(UnixCCParserTest, IgnoreDiagnostics) {
  const string output = 
    "\"src/unixcc_parser_test.cc\", line 30: Error: err is not defined.\n"
    "1 Error(s) detected.\n";

  Parse(output);
  EXPECT_EQ(filtered_output_, output);
  EXPECT_TRUE(parser_.includes_.empty());
}

TEST_F(UnixCCParserTest, RealExample) {
  const string output = 
    "\"src/unixcc_parser.cc\", line 18: Warning: Identifier expected instead of \"}\".\n"
    "src/unixcc_parser.h\n"
    "\t/opt/solarisstudio12.4/lib/compilers/include/CC/stlport4/string\n"
    "\t\t/opt/solarisstudio12.4/lib/compilers/include/CC/stlport4/stl/_prolog.h\n"
    "\t\t\t/opt/solarisstudio12.4/lib/compilers/include/CC/stlport4/stl/_config.h\n"
    "\t\t\t\t/opt/solarisstudio12.4/lib/compilers/include/CC/stlport4/stl_user_config.h\n"
    /* ... */
    "\t\t\t\t\t/opt/solarisstudio12.4/lib/compilers/include/CC/stlport4/config/stl_sunpro.h\n"
    /* ... */
    "/usr/include/sys/ctype.h\n"
    "\"src/unixcc_parser.cc\", line 70: Warning: Identifier expected instead of \"}\".\n"
    "2 Warning(s) detected.\n";
  const string expected_filtered_output = 
    "\"src/unixcc_parser.cc\", line 18: Warning: Identifier expected instead of \"}\".\n"
    "\"src/unixcc_parser.cc\", line 70: Warning: Identifier expected instead of \"}\".\n"
    "2 Warning(s) detected.\n";

  Parse(output);
  EXPECT_EQ(expected_filtered_output, filtered_output_);
  EXPECT_EQ(parser_.includes_.size(), 7);
}
