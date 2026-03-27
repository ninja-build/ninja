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

#include "string_piece.h"

#include "test.h"

TEST(StringPieceTest, BasicTests) {
  {
    StringPiece empty;
    EXPECT_EQ(empty.size(), 0);
    EXPECT_EQ(empty.empty(), true);
    EXPECT_EQ(empty.begin(), empty.end());
    EXPECT_EQ(empty, empty);
    EXPECT_EQ(empty, "");
    EXPECT_EQ("", empty);
    EXPECT_FALSE("" != empty);
    EXPECT_FALSE(empty != "");
    EXPECT_FALSE(empty != empty);
  }

  {
    char source[] = { 'a', 'b', 'c' };
    const StringPiece str(source, 3);
    EXPECT_EQ(str.size(), 3);
    EXPECT_EQ(str.empty(), false);
    EXPECT_EQ(str.begin(), source);
    EXPECT_EQ(str.end(), source + 3);
    EXPECT_EQ(str[0], 'a');
    EXPECT_EQ(str[1], 'b');
    EXPECT_EQ(str[2], 'c');
    EXPECT_EQ(str, "abc");
    EXPECT_EQ("abc", str);
    EXPECT_EQ(str, str);
    EXPECT_TRUE("ABC" != str);
    EXPECT_TRUE(str != "def");
    EXPECT_EQ(str.AsString(), std::string("abc"));

    // Check StringPiece is a view into the original string.
    source[1] = 'x';
    EXPECT_EQ(str[1], 'x');
    EXPECT_EQ(str, "axc");
  }

  {
    // Check construction from const char*.
    const StringPiece str("abcd\0ef");
    EXPECT_EQ(str.size(), 4);
    EXPECT_EQ(str, "abcd");
  }

  {
    // Check construction from std::string.
    const std::string original("xyz");
    const StringPiece str(original);
    EXPECT_EQ(str.size(), original.size());
    EXPECT_EQ(str, "xyz");
  }
}

TEST(StringPieceTest, substr) {
  EXPECT_EQ(StringPiece().substr(0), "");
  EXPECT_EQ(StringPiece().substr(0, 0), "");
  EXPECT_EQ(StringPiece().substr(0, 1), "");
  EXPECT_EQ(StringPiece().substr(0, 2), "");

  EXPECT_EQ(StringPiece("abc").substr(0), "abc");
  EXPECT_EQ(StringPiece("abc").substr(0, 0), "");
  EXPECT_EQ(StringPiece("abc").substr(0, 1), "a");
  EXPECT_EQ(StringPiece("abc").substr(0, 2), "ab");
  EXPECT_EQ(StringPiece("abc").substr(0, 3), "abc");
  EXPECT_EQ(StringPiece("abc").substr(0, 4), "abc");
  EXPECT_EQ(StringPiece("abc").substr(1), "bc");
  EXPECT_EQ(StringPiece("abc").substr(1, 0), "");
  EXPECT_EQ(StringPiece("abc").substr(1, 1), "b");
  EXPECT_EQ(StringPiece("abc").substr(1, 2), "bc");
  EXPECT_EQ(StringPiece("abc").substr(2), "c");
  EXPECT_EQ(StringPiece("abc").substr(3), "");
}
