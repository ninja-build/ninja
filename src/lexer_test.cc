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

#include "lexer.h"

#include "eval_env.h"
#include "test.h"

std::string tok(Lexer::Token t) {
  const char *str = Lexer::TokenName(t);
  if (!str)
    return "TokenOutOfRange: " + std::to_string(t);
  return str;
}

#define EXPECT_EQ_TOK(t1, t2) \
  EXPECT_EQ(tok(t1), tok(t2))

TEST(Lexer, ReadVarValue) {
  Lexer lexer("plain text $var $VaR ${x}\n");
  EvalString eval;
  std::string err;
  EXPECT_TRUE(lexer.ReadVarValue(&eval, &err));
  EXPECT_EQ("", err);
  EXPECT_EQ("[plain text ][$var][ ][$VaR][ ][$x]",
            eval.Serialize());
}

TEST(Lexer, ReadEvalStringEscapes) {
  Lexer lexer("$ $$ab c$: $\ncde\n");
  EvalString eval;
  std::string err;
  EXPECT_TRUE(lexer.ReadVarValue(&eval, &err));
  EXPECT_EQ("", err);
  EXPECT_EQ("[ $ab c: cde]",
            eval.Serialize());
}

TEST(Lexer, ReadIdent) {
  Lexer lexer("foo baR baz_123 foo-bar");
  std::string ident;
  EXPECT_TRUE(lexer.ReadIdent(&ident));
  EXPECT_EQ("foo", ident);
  EXPECT_TRUE(lexer.ReadIdent(&ident));
  EXPECT_EQ("baR", ident);
  EXPECT_TRUE(lexer.ReadIdent(&ident));
  EXPECT_EQ("baz_123", ident);
  EXPECT_TRUE(lexer.ReadIdent(&ident));
  EXPECT_EQ("foo-bar", ident);
}

TEST(Lexer, ReadIdentCurlies) {
  // Verify that ReadIdent includes dots in the name,
  // but in an expansion $bar.dots stops at the dot.
  Lexer lexer("foo.dots $bar.dots ${bar.dots}\n");
  std::string ident;
  EXPECT_TRUE(lexer.ReadIdent(&ident));
  EXPECT_EQ("foo.dots", ident);

  EvalString eval;
  std::string err;
  EXPECT_TRUE(lexer.ReadVarValue(&eval, &err));
  EXPECT_EQ("", err);
  EXPECT_EQ("[$bar][.dots ][$bar.dots]",
            eval.Serialize());
}

TEST(Lexer, Error) {
  Lexer lexer("foo$\nbad $");
  EvalString eval;
  std::string err;
  ASSERT_FALSE(lexer.ReadVarValue(&eval, &err));
  EXPECT_EQ("input:2: bad $-escape (literal $ must be written as $$)\n"
            "bad $\n"
            "    ^ near here"
            , err);
}

TEST(Lexer, CommentEOF) {
  // Verify we don't run off the end of the string when the EOF is
  // mid-comment.
  Lexer lexer("# foo");
  EXPECT_EQ_TOK(Lexer::ERROR, lexer.ReadToken());
}

TEST(Lexer, Tabs) {
  Lexer lexer("rule foo\n"
              "\tcommand = foobin $in");

  EXPECT_EQ_TOK(Lexer::RULE, lexer.ReadToken());
  EXPECT_EQ_TOK(Lexer::IDENT, lexer.ReadToken());
  EXPECT_EQ_TOK(Lexer::NEWLINE, lexer.ReadToken());
  EXPECT_EQ_TOK(Lexer::INDENT, lexer.ReadToken());
  EXPECT_EQ_TOK(Lexer::IDENT, lexer.ReadToken());
  EXPECT_EQ_TOK(Lexer::EQUALS, lexer.ReadToken());
}

TEST(Lexer, TabsInVars) {
  Lexer lexer("cflags =\n"
              "\t-std=c11");

  EXPECT_EQ_TOK(Lexer::IDENT, lexer.ReadToken());
  EXPECT_EQ_TOK(Lexer::EQUALS, lexer.ReadToken());
  EXPECT_EQ_TOK(Lexer::NEWLINE, lexer.ReadToken());
  EXPECT_EQ_TOK(Lexer::INDENT, lexer.ReadToken());
  EXPECT_EQ_TOK(Lexer::IDENT, lexer.ReadToken());
}
