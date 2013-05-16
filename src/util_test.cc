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

TEST(CanonicalizePath, PathSamples) {
  string path;
  string err;

  EXPECT_FALSE(CanonicalizePath(&path, &err));
  EXPECT_EQ("empty path", err);

  path = "foo.h"; err = "";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("foo.h", path);

  path = "./foo.h";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("foo.h", path);

  path = "./foo/./bar.h";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("foo/bar.h", path);

  path = "./x/foo/../bar.h";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("x/bar.h", path);

  path = "./x/foo/../../bar.h";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("bar.h", path);

  path = "foo//bar";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("foo/bar", path);

  path = "foo//.//..///bar";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("bar", path);

  path = "./x/../foo/../../bar.h";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("../bar.h", path);

  path = "foo/./.";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("foo", path);

  path = "foo/bar/..";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("foo", path);

  path = "foo/.hidden_bar";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("foo/.hidden_bar", path);

  path = "/foo";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("/foo", path);

  path = "//foo";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
#ifdef _WIN32
  EXPECT_EQ("//foo", path);
#else
  EXPECT_EQ("/foo", path);
#endif

  path = "/";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("", path);
}

TEST(CanonicalizePath, EmptyResult) {
  string path;
  string err;

  EXPECT_FALSE(CanonicalizePath(&path, &err));
  EXPECT_EQ("empty path", err);

  path = ".";
  EXPECT_FALSE(CanonicalizePath(&path, &err));
  EXPECT_EQ("path canonicalizes to the empty path", err);

  path = "./.";
  EXPECT_FALSE(CanonicalizePath(&path, &err));
  EXPECT_EQ("path canonicalizes to the empty path", err);
}

TEST(CanonicalizePath, UpDir) {
  string path, err;
  path = "../../foo/bar.h";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("../../foo/bar.h", path);

  path = "test/../../foo/bar.h";
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("../foo/bar.h", path);
}

TEST(CanonicalizePath, AbsolutePath) {
  string path = "/usr/include/stdio.h";
  string err;
  EXPECT_TRUE(CanonicalizePath(&path, &err));
  EXPECT_EQ("/usr/include/stdio.h", path);
}

TEST(CanonicalizePath, NotNullTerminated) {
  string path;
  string err;
  size_t len;

  path = "foo/. bar/.";
  len = strlen("foo/.");  // Canonicalize only the part before the space.
  EXPECT_TRUE(CanonicalizePath(&path[0], &len, &err));
  EXPECT_EQ(strlen("foo"), len);
  EXPECT_EQ("foo/. bar/.", string(path));

  path = "foo/../file bar/.";
  len = strlen("foo/../file");
  EXPECT_TRUE(CanonicalizePath(&path[0], &len, &err));
  EXPECT_EQ(strlen("file"), len);
  EXPECT_EQ("file ./file bar/.", string(path));
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
