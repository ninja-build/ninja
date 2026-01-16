// Copyright 2026 Google Inc. All Rights Reserved.
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

#include "evalstring.h"

#include "test.h"

using Token = EvalString::TokenType;

namespace {

void build(EvalStringBuilder* builder,
           const std::vector<std::pair<StringPiece, Token>>& tokens) {
  for (const auto& token : tokens) {
    if (token.second == Token::RAW) {
      builder->AddText(token.first);
    } else {
      builder->AddSpecial(token.first);
    }
  }
}

bool roundTripEqual(
    const std::vector<std::pair<StringPiece, Token>>& input,
    const std::vector<std::pair<StringPiece, Token>>& expected) {
  EvalStringBuilder builder;
  build(&builder, input);
  const EvalString& str = builder.str();
  return std::equal(str.begin(), str.end(), expected.begin(), expected.end());
}

bool roundTripEqual(const std::vector<std::pair<StringPiece, Token>>& input) {
  return roundTripEqual(input, input);
}

} // anonymous namespace

TEST(EvalString, DefaultCtor) {
  const EvalString str;
  EXPECT_EQ(str.begin(), str.end());
  EXPECT_TRUE(str.empty());
  EXPECT_EQ(str.Serialize(), "");
  EXPECT_EQ(str.Unparse(), "");

  const EvalStringBuilder builder;
  EXPECT_TRUE(builder.empty());
  EXPECT_TRUE(builder.str().empty());
}

TEST(EvalString, RoundTrip) {
  EXPECT_TRUE(roundTripEqual({}));
  EXPECT_TRUE(roundTripEqual({ { "txt", Token::RAW } }));
  EXPECT_TRUE(roundTripEqual({ { "$", Token::RAW } }));
  EXPECT_TRUE(roundTripEqual({ { "var", Token::SPECIAL } }));
  EXPECT_TRUE(roundTripEqual(
      { { "var", Token::SPECIAL }, { "text_after", Token::RAW } }));
  EXPECT_TRUE(roundTripEqual(
      { { "text_before", Token::RAW }, { "var", Token::SPECIAL } }));
  EXPECT_TRUE(
      roundTripEqual({ { "foo", Token::SPECIAL }, { "bar", Token::SPECIAL } }));
  EXPECT_TRUE(roundTripEqual({ { "a", Token::RAW }, { "b", Token::RAW } },
                             { { "ab", Token::RAW } }));
  EXPECT_TRUE(roundTripEqual({ { "a", Token::RAW },
                               { "b", Token::RAW },
                               { "c", Token::RAW },
                               { "var", Token::SPECIAL },
                               { "var2", Token::SPECIAL },
                               { "d", Token::RAW },
                               { "e", Token::RAW } },
                             { { "abc", Token::RAW },
                               { "var", Token::SPECIAL },
                               { "var2", Token::SPECIAL },
                               { "de", Token::RAW } }));
}

TEST(EvalString, Serializing) {
  EvalStringBuilder builder;
  build(&builder, { { "txt", Token::RAW } });
  EXPECT_EQ(builder.str().Serialize(), "[txt]");
  EXPECT_EQ(builder.str().Unparse(), "txt");
  builder.Clear();

  build(&builder, { { "var", Token::SPECIAL } });
  EXPECT_EQ(builder.str().Serialize(), "[$var]");
  EXPECT_EQ(builder.str().Unparse(), "${var}");
  builder.Clear();

  build(&builder, { { "var", Token::SPECIAL }, { "txt", Token::RAW } });
  EXPECT_EQ(builder.str().Serialize(), "[$var][txt]");
  EXPECT_EQ(builder.str().Unparse(), "${var}txt");
  builder.Clear();

  build(&builder, { { "1", Token::RAW },
                    { "2", Token::SPECIAL },
                    { "3", Token::RAW } });
  EXPECT_EQ(builder.str().Serialize(), "[1][$2][3]");
  EXPECT_EQ(builder.str().Unparse(), "1${2}3");
  builder.Clear();
}
