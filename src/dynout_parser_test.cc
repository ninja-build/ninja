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

#include "dynout_parser.h"

#include "test.h"

using namespace std;

struct DynoutParserTest : public testing::Test {

  VirtualFileSystem fs_;
};

TEST_F(DynoutParserTest, Empty) {
  std::string input;
  string err;
  std::vector<StringPiece> result;
  EXPECT_TRUE(DynoutParser::Parse(input, result, &err));
  ASSERT_EQ(err, "");
  ASSERT_TRUE(result.empty());
}

TEST_F(DynoutParserTest, MultipleEntries) {
  std::string input = "file1\nfile2\nfile3";
  string err;
  std::vector<StringPiece> result;
  EXPECT_TRUE(DynoutParser::Parse(input, result, &err));
  ASSERT_EQ(err, "");
  ASSERT_EQ(result[0], "file1");
  ASSERT_EQ(result[1], "file2");
  ASSERT_EQ(result[2], "file3");
}

TEST_F(DynoutParserTest, EmptyLines) {
  std::string input = "\nfile1\n\n\nfile2\n\n";
  string err;
  std::vector<StringPiece> result;
  EXPECT_TRUE(DynoutParser::Parse(input, result, &err));
  ASSERT_EQ(err, "");
  ASSERT_EQ(result[0], "file1");
  ASSERT_EQ(result[1], "file2");
}

TEST_F(DynoutParserTest, CRLF) {
  std::string input = "\r\nfile1\r\n\r\nfile2\r\n";
  string err;
  std::vector<StringPiece> result;
  EXPECT_TRUE(DynoutParser::Parse(input, result, &err));
  ASSERT_EQ(err, "");
  ASSERT_EQ(result[0], "file1");
  ASSERT_EQ(result[1], "file2");
}
