// Copyright 2017 Google Inc. All Rights Reserved.
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

#include "string_piece_util.h"

#include "test.h"

TEST(StringPieceUtilTest, SplitStringPiece) {
  {
    string input("a:b:c");
    vector<StringPiece> list = SplitStringPiece(input, ':');

    EXPECT_EQ(list.size(), 3);

    EXPECT_EQ(list[0], "a");
    EXPECT_EQ(list[1], "b");
    EXPECT_EQ(list[2], "c");
  }

  {
    string empty("");
    vector<StringPiece> list = SplitStringPiece(empty, ':');

    EXPECT_EQ(list.size(), 1);

    EXPECT_EQ(list[0], "");
  }

  {
    string one("a");
    vector<StringPiece> list = SplitStringPiece(one, ':');

    EXPECT_EQ(list.size(), 1);

    EXPECT_EQ(list[0], "a");
  }

  {
    string sep_only(":");
    vector<StringPiece> list = SplitStringPiece(sep_only, ':');

    EXPECT_EQ(list.size(), 2);

    EXPECT_EQ(list[0], "");
    EXPECT_EQ(list[1], "");
  }

  {
    string sep(":a:b:c:");
    vector<StringPiece> list = SplitStringPiece(sep, ':');

    EXPECT_EQ(list.size(), 5);

    EXPECT_EQ(list[0], "");
    EXPECT_EQ(list[1], "a");
    EXPECT_EQ(list[2], "b");
    EXPECT_EQ(list[3], "c");
    EXPECT_EQ(list[4], "");
  }
}

TEST(StringPieceUtilTest, JoinStringPiece) {
  {
    string input("a:b:c");
    vector<StringPiece> list = SplitStringPiece(input, ':');

    EXPECT_EQ("a:b:c", JoinStringPiece(list, ':'));
    EXPECT_EQ("a/b/c", JoinStringPiece(list, '/'));
  }

  {
    string empty("");
    vector<StringPiece> list = SplitStringPiece(empty, ':');

    EXPECT_EQ("", JoinStringPiece(list, ':'));
  }

  {
    vector<StringPiece> empty_list;

    EXPECT_EQ("", JoinStringPiece(empty_list, ':'));
  }

  {
    string one("a");
    vector<StringPiece> single_list = SplitStringPiece(one, ':');

    EXPECT_EQ("a", JoinStringPiece(single_list, ':'));
  }

  {
    string sep(":a:b:c:");
    vector<StringPiece> list = SplitStringPiece(sep, ':');

    EXPECT_EQ(":a:b:c:", JoinStringPiece(list, ':'));
  }
}

TEST(StringPieceUtilTest, ToLowerASCII) {
  EXPECT_EQ('a', ToLowerASCII('A'));
  EXPECT_EQ('z', ToLowerASCII('Z'));
  EXPECT_EQ('a', ToLowerASCII('a'));
  EXPECT_EQ('z', ToLowerASCII('z'));
  EXPECT_EQ('/', ToLowerASCII('/'));
  EXPECT_EQ('1', ToLowerASCII('1'));
}

TEST(StringPieceUtilTest, EqualsCaseInsensitiveASCII) {
  EXPECT_TRUE(EqualsCaseInsensitiveASCII("abc", "abc"));
  EXPECT_TRUE(EqualsCaseInsensitiveASCII("abc", "ABC"));
  EXPECT_TRUE(EqualsCaseInsensitiveASCII("abc", "aBc"));
  EXPECT_TRUE(EqualsCaseInsensitiveASCII("AbC", "aBc"));
  EXPECT_TRUE(EqualsCaseInsensitiveASCII("", ""));

  EXPECT_FALSE(EqualsCaseInsensitiveASCII("a", "ac"));
  EXPECT_FALSE(EqualsCaseInsensitiveASCII("/", "\\"));
  EXPECT_FALSE(EqualsCaseInsensitiveASCII("1", "10"));
}
