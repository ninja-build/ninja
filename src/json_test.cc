// Copyright 2021 Google Inc. All Rights Reserved.
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

#include "json.h"

#include <cstdio>
#include <string>

#include "test.h"

struct JSONTest : public testing::Test {
  void SetUp() override;
  void TearDown() override;
  std::string captured_string();

  FILE* file_;
  char* file_buf_;
  size_t file_buf_size_;
};

void JSONTest::SetUp() {
  file_ = open_memstream(&file_buf_, &file_buf_size_);
  EXPECT_TRUE(file_);
}

void JSONTest::TearDown() {
  fclose(file_);
  free(file_buf_);
}

std::string JSONTest::captured_string() {
  fflush(file_);
  return std::string(file_buf_, file_buf_size_);
}

TEST_F(JSONTest, RegularAscii) {
  EncodeJSONString(file_, "foo bar");
  EXPECT_EQ("foo bar", captured_string());
}

TEST_F(JSONTest, EscapedChars) {
  EncodeJSONString(file_, "\"\\\b\f\n\r\t");
  EXPECT_EQ(R"(\"\\\b\f\n\r\t)", captured_string());
}

// codepoints between 0 and 0x1f should be escaped
TEST_F(JSONTest, ControlChars) {
  EncodeJSONString(file_, "\x01\x1f");
  EXPECT_EQ(R"(\u0001\u001f)", captured_string());
}

// Leave them alone as JSON accepts unicode literals
// out of control character range
TEST_F(JSONTest, UTF8) {
  const char* utf8str = "\xe4\xbd\xa0\xe5\xa5\xbd";
  EncodeJSONString(file_, utf8str);
  EXPECT_EQ(utf8str, captured_string());
}
