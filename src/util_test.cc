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

#include "util.h"

#include "test.h"

namespace {

// Rather than cluttering up the test data, wrap here and convert / to \.
bool CanonPath(string* str, string* err) {
#ifdef _WIN32
  for (size_t i = 0; i < str->size(); ++i)
    if (str->operator[](i) == '/')
      str->operator[](i) = '\\';
#endif
  return CanonicalizePath(str, err);
}

bool CanonPath(char* str, int* len, string* err) {
#ifdef _WIN32
  for (int i = 0; i < *len; ++i)
    if (str[i] == '/')
      str[i] = '\\';
#endif
  return CanonicalizePath(str, len, err);
}

string SLASH(string in, int len = -1) {
#ifdef _WIN32
  if (len == -1)
    len = static_cast<int>(in.size());
  for (int i = 0; i < len; ++i)
    if (in[i] == '/')
      in[i] = '\\';
#endif
  return in;
}

}

TEST(CanonicalizePath, PathSamples) {
  string path;
  string err;

  EXPECT_FALSE(CanonPath(&path, &err));
  EXPECT_EQ("empty path", err);

  path = "foo.h"; err = "";
  EXPECT_TRUE(CanonPath(&path, &err));
  EXPECT_EQ("foo.h", path);

  path = "./foo.h";
  EXPECT_TRUE(CanonPath(&path, &err));
  EXPECT_EQ("foo.h", path);

  path = "./foo/./bar.h";
  EXPECT_TRUE(CanonPath(&path, &err));
  EXPECT_EQ(SLASH("foo/bar.h"), path);

  path = "./x/foo/../bar.h";
  EXPECT_TRUE(CanonPath(&path, &err));
  EXPECT_EQ(SLASH("x/bar.h"), path);

  path = "./x/foo/../../bar.h";
  EXPECT_TRUE(CanonPath(&path, &err));
  EXPECT_EQ(SLASH("bar.h"), path);

  path = "foo//bar";
  EXPECT_TRUE(CanonPath(&path, &err));
  EXPECT_EQ(SLASH("foo/bar"), path);

  path = "foo//.//..///bar";
  EXPECT_TRUE(CanonPath(&path, &err));
  EXPECT_EQ("bar", path);

  path = "./x/../foo/../../bar.h";
  EXPECT_TRUE(CanonPath(&path, &err));
  EXPECT_EQ(SLASH("../bar.h"), path);

  path = "foo/./.";
  EXPECT_TRUE(CanonPath(&path, &err));
  EXPECT_EQ("foo", path);

  path = "foo/bar/..";
  EXPECT_TRUE(CanonPath(&path, &err));
  EXPECT_EQ("foo", path);

  path = "foo/.hidden_bar";
  EXPECT_TRUE(CanonPath(&path, &err));
  EXPECT_EQ(SLASH("foo/.hidden_bar"), path);
}

TEST(CanonicalizePath, EmptyResult) {
  string path;
  string err;

  EXPECT_FALSE(CanonPath(&path, &err));
  EXPECT_EQ("empty path", err);

  path = ".";
  EXPECT_FALSE(CanonPath(&path, &err));
  EXPECT_EQ("path canonicalizes to the empty path", err);

  path = "./.";
  EXPECT_FALSE(CanonPath(&path, &err));
  EXPECT_EQ("path canonicalizes to the empty path", err);
}

TEST(CanonicalizePath, UpDir) {
  std::string path, err;
  path = "../../foo/bar.h";
  EXPECT_TRUE(CanonPath(&path, &err));
  EXPECT_EQ(SLASH("../../foo/bar.h"), path);

  path = "test/../../foo/bar.h";
  EXPECT_TRUE(CanonPath(&path, &err));
  EXPECT_EQ(SLASH("../foo/bar.h"), path);
}

TEST(CanonicalizePath, AbsolutePath) {
  string path = "/usr/include/stdio.h";
  string err;
  EXPECT_TRUE(CanonPath(&path, &err));
  EXPECT_EQ(SLASH("/usr/include/stdio.h"), path);
}

TEST(CanonicalizePath, NotNullTerminated) {
  string path;
  string err;
  int len, orig_len;

  path = "foo/. bar/.";
  orig_len = len = strlen("foo/.");  // Canonicalize only the part before the space.
  EXPECT_TRUE(CanonPath(&path[0], &len, &err));
  EXPECT_EQ(strlen("foo"), static_cast<size_t>(len));
  EXPECT_EQ(SLASH("foo/. bar/.", orig_len), string(path));

  path = "foo/../file bar/.";
  orig_len = len = strlen("foo/../file");
  EXPECT_TRUE(CanonPath(&path[0], &len, &err));
  EXPECT_EQ(strlen("file"), static_cast<size_t>(len));
  EXPECT_EQ(SLASH("file ./file bar/.", orig_len), string(path));
}

TEST(StripAnsiEscapeCodes, EscapeAtEnd) {
  string stripped = StripAnsiEscapeCodes("foo\33");
  EXPECT_EQ("foo", stripped);

  stripped = StripAnsiEscapeCodes("foo\33[");
  EXPECT_EQ("foo", stripped);
}

TEST(StripAnsiEscapeCodes, StripColors) {
  // An actual clang warning.
  string input = "\33[1maffixmgr.cxx:286:15: \33[0m\33[0;1;35mwarning: "
                 "\33[0m\33[1musing the result... [-Wparentheses]\33[0m";
  string stripped = StripAnsiEscapeCodes(input);
  EXPECT_EQ("affixmgr.cxx:286:15: warning: using the result... [-Wparentheses]",
            stripped);
}

TEST(ElideMiddle, NothingToElide) {
  string input = "Nothing to elide in this short string.";
  EXPECT_EQ(input, ElideMiddle(input, 80));
}

TEST(ElideMiddle, ElideInTheMiddle) {
  string input = "01234567890123456789";
  string elided = ElideMiddle(input, 10);
  EXPECT_EQ("012...789", elided);
}
