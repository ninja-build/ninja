#include "parsers.h"

#include <gtest/gtest.h>

#include "ninja.h"

static void AssertParse(State* state, const char* input) {
  ManifestParser parser(state);
  string err;
  ASSERT_TRUE(parser.Parse(input, &err)) << err;
  ASSERT_EQ("", err);
}

TEST(Parser, Empty) {
  State state;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state, ""));
}

TEST(Parser, Rules) {
  State state;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state,
"rule cat\n"
"  command = cat @in > $out\n"
"\n"
"rule date\n"
"  command = date > $out\n"
"\n"
"build result: cat in_1.cc in-2.O\n"));

  ASSERT_EQ(2, state.rules_.size());
  Rule* rule = state.rules_.begin()->second;
  EXPECT_EQ("cat", rule->name_);
  EXPECT_EQ("cat @in > $out", rule->command_.unparsed());
}

TEST(Parser, Variables) {
  State state;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state,
"rule link\n"
"  command = ld $extra $with_under -o $out @in\n"
"\n"
"extra = -pthread\n"
"with_under = -under\n"
"build a: link b c\n"));

  ASSERT_EQ(1, state.edges_.size());
  Edge* edge = state.edges_[0];
  EXPECT_EQ("ld -pthread -under -o a b c", edge->EvaluateCommand());
}

TEST(Parser, Continuation) {
  State state;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state,
"rule link\n"
"  command = foo bar \\\n"
"    baz\n"
"\n"
"build a: link c \\\n"
" d e f\n"));

  ASSERT_EQ(1, state.rules_.size());
  Rule* rule = state.rules_.begin()->second;
  EXPECT_EQ("link", rule->name_);
  EXPECT_EQ("foo bar baz", rule->command_.unparsed());
}

TEST(Parser, Comment) {
  State state;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state,
"# this is a comment\n"
"foo = not # a comment\n"));
  EXPECT_EQ("not # a comment", state.env_["foo"]);
}

TEST(Parser, Errors) {
  {
    ManifestParser parser(NULL);
    string err;
    EXPECT_FALSE(parser.Parse("foobar", &err));
    EXPECT_EQ("line 1, col 7: expected '=', got eof", err);
  }

  {
    ManifestParser parser(NULL);
    string err;
    EXPECT_FALSE(parser.Parse("x 3", &err));
    EXPECT_EQ("line 1, col 3: expected '=', got '3'", err);
  }

  {
    ManifestParser parser(NULL);
    string err;
    EXPECT_FALSE(parser.Parse("x = 3", &err));
    EXPECT_EQ("line 1, col 6: expected newline, got eof", err);
  }

  {
    State state;
    ManifestParser parser(&state);
    string err;
    EXPECT_FALSE(parser.Parse("x = 3\ny 2", &err));
    EXPECT_EQ("line 2, col 3: expected '=', got '2'", err);
  }

  {
    State state;
    ManifestParser parser(&state);
    string err;
    EXPECT_FALSE(parser.Parse("build x: y z\n", &err));
    EXPECT_EQ("line 1, col 10: unknown build rule 'y'", err);
  }

  {
    State state;
    ManifestParser parser(&state);
    string err;
    EXPECT_FALSE(parser.Parse("build x:: y z\n", &err));
    EXPECT_EQ("line 1, col 9: expected build command name", err);
  }

  {
    State state;
    ManifestParser parser(&state);
    string err;
    EXPECT_FALSE(parser.Parse("rule cat\n  command = cat ok\n"
                              "build x: cat \\\n :\n",
                              &err));
    EXPECT_EQ("line 4, col 2: expected newline, got ':'", err);
  }

  {
    State state;
    ManifestParser parser(&state);
    string err;
    EXPECT_FALSE(parser.Parse("rule cat\n"
                              "  foo = bar\n",
                              &err));
    EXPECT_EQ("line 3, col 1: expected 'command =' line", err);
  }

  {
    State state;
    ManifestParser parser(&state);
    string err;
    EXPECT_FALSE(parser.Parse("rule %foo\n",
                              &err));
    EXPECT_EQ("line 1, col 6: expected rule name, got unknown '%'", err);
  }

  {
    State state;
    ManifestParser parser(&state);
    string err;
    EXPECT_FALSE(parser.Parse("rule cc\n  command = foo\n  depfile = bar\n"
                              "build a.o b.o: cc c.cc\n",
                              &err));
    EXPECT_EQ("line 4, col 16: dependency files only work with "
              "single-output rules", err);
  }
}

TEST(Parser, BuildDir) {
  State state;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state,
"builddir = out\n"
"rule cat\n"
"  command = cat @in > $out\n"
"build @bin: cat @a.o\n"
"build @a.o: cat a.cc\n"));
  ASSERT_TRUE(state.LookupNode("out/a.o"));
}

TEST(MakefileParser, Basic) {
  MakefileParser parser;
  string err;
  EXPECT_TRUE(parser.Parse(
"build/ninja.o: ninja.cc ninja.h eval_env.h manifest_parser.h\n",
      &err));
  ASSERT_EQ("", err);
}

