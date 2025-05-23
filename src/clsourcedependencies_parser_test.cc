// Copyright 2025 Google Inc. All Rights Reserved.
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

#include "clsourcedependencies_parser.h"

#include "test.h"

TEST(ParseCLSourceDependencies, ParseInvalidJSON) {
  std::string err;
  std::vector<std::string> includes;

  StringPiece content = "this is not JSON";
  ASSERT_FALSE(ParseCLSourceDependencies(content, &includes, &err));
  EXPECT_EQ("sourceDependencies is not valid JSON: Invalid value.", err);
}

TEST(ParseCLSourceDependencies, ParseRootWrongType) {
  std::string err;
  std::vector<std::string> includes;

  StringPiece content = "[]";
  ASSERT_FALSE(ParseCLSourceDependencies(content, &includes, &err));
  EXPECT_EQ("sourceDependencies is not an object", err);
}

TEST(ParseCLSourceDependencies, ParseMissingVersion) {
  std::string err;
  std::vector<std::string> includes;

  StringPiece content = "{}";
  ASSERT_FALSE(ParseCLSourceDependencies(content, &includes, &err));
  EXPECT_EQ("sourceDependencies is missing Version", err);
}

TEST(ParseCLSourceDependencies, ParseVersionWrongType) {
  std::string err;
  std::vector<std::string> includes;

  StringPiece content = R"({"Version": 1.0})";
  ASSERT_FALSE(ParseCLSourceDependencies(content, &includes, &err));
  EXPECT_EQ("sourceDependencies Version is not a string", err);
}

TEST(ParseCLSourceDependencies, ParseWrongVersion) {
  std::string err;
  std::vector<std::string> includes;

  StringPiece content = R"({"Version": "2.0"})";
  ASSERT_FALSE(ParseCLSourceDependencies(content, &includes, &err));
  EXPECT_EQ("sourceDependencies Version is 2.0, but expected 1.x", err);
}

TEST(ParseCLSourceDependencies, ParseMissingData) {
  std::string err;
  std::vector<std::string> includes;

  StringPiece content = R"({"Version": "1.0"})";
  ASSERT_FALSE(ParseCLSourceDependencies(content, &includes, &err));
  EXPECT_EQ("sourceDependencies is missing Data", err);
}

TEST(ParseCLSourceDependencies, ParseDataWrongType) {
  std::string err;
  std::vector<std::string> includes;

  StringPiece content = R"({"Version": "1.0", "Data": true})";
  ASSERT_FALSE(ParseCLSourceDependencies(content, &includes, &err));
  EXPECT_EQ("sourceDependencies Data is not an object", err);
}

TEST(ParseCLSourceDependencies, ParseDataMissingIncludes) {
  std::string err;
  std::vector<std::string> includes;

  StringPiece content = R"({"Version": "1.0", "Data": {}})";
  ASSERT_FALSE(ParseCLSourceDependencies(content, &includes, &err));
  EXPECT_EQ("sourceDependencies Data is missing Includes", err);
}

TEST(ParseCLSourceDependencies, ParseDataIncludesWrongType) {
  std::string err;
  std::vector<std::string> includes;

  StringPiece content = R"({"Version": "1.0", "Data": {"Includes": {}}})";
  ASSERT_FALSE(ParseCLSourceDependencies(content, &includes, &err));
  EXPECT_EQ("sourceDependencies Data/Includes is not an array", err);
}

TEST(ParseCLSourceDependencies, ParseBadSingleInclude) {
  std::string err;
  std::vector<std::string> includes;

  StringPiece content = R"({"Version": "1.0", "Data": {"Includes": [23]}})";
  ASSERT_FALSE(ParseCLSourceDependencies(content, &includes, &err));
  EXPECT_EQ("sourceDependencies Data/Includes element is not a string", err);
}

TEST(ParseCLSourceDependencies, ParseSimple) {
  std::string err;
  std::vector<std::string> includes;

  StringPiece content =
      R"({"Version": "1.0", "Data": {"Includes": ["c:\\test.cpp"]}})";
  ASSERT_TRUE(ParseCLSourceDependencies(content, &includes, &err));
  ASSERT_EQ(1u, includes.size());
  EXPECT_EQ("c:\\test.cpp", includes[0]);
}

TEST(ParseCLSourceDependencies, ParseReal) {
  StringPiece content =
      R"========({
  "Version": "1.0",
  "Data": {
    "Includes": [
      "c:\\program files (x86)\\windows kits\\10\\include\\10.0.18362.0\\locale.h",
      "c:\\constants.h",
      "c:\\test.h"
    ],
    "Modules": []
  }
})========";

  std::string err;
  std::vector<std::string> includes;
  ASSERT_TRUE(ParseCLSourceDependencies(content, &includes, &err));

  ASSERT_EQ(2u, includes.size());
  EXPECT_EQ("c:\\constants.h", includes[0]);
  EXPECT_EQ("c:\\test.h", includes[1]);
}
