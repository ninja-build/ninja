#include "parsers.h"

#include <gtest/gtest.h>

#include "ninja.h"

struct ParserTest : public testing::Test,
                    public ManifestParser::FileReader {
  void AssertParse(const char* input) {
    ManifestParser parser(&state, this);
    string err;
    ASSERT_TRUE(parser.Parse(input, &err)) << err;
    ASSERT_EQ("", err);
  }

  virtual bool ReadFile(const string& path, string* content, string* err) {
    files_read_.push_back(path);
    map<string, string>::iterator i = files_.find(path);
    if (i == files_.end()) {
      *err = "file not found";
      return false;
    }
    *content = i->second;
    return true;
  }

  State state;
  map<string, string> files_;
  vector<string> files_read_;
};

TEST_F(ParserTest, Empty) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(""));
}

TEST_F(ParserTest, Rules) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule cat\n"
"  command = cat $in > $out\n"
"\n"
"rule date\n"
"  command = date > $out\n"
"\n"
"build result: cat in_1.cc in-2.O\n"));

  ASSERT_EQ(2, state.rules_.size());
  Rule* rule = state.rules_.begin()->second;
  EXPECT_EQ("cat", rule->name_);
  EXPECT_EQ("cat $in > $out", rule->command_.unparsed());
}

TEST_F(ParserTest, Variables) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule link\n"
"  command = ld $extra $with_under -o $out $in\n"
"\n"
"extra = -pthread\n"
"with_under = -under\n"
"build a: link b c\n"));

  ASSERT_EQ(1, state.edges_.size());
  Edge* edge = state.edges_[0];
  EXPECT_EQ("ld -pthread -under -o a b c", edge->EvaluateCommand());
}

TEST_F(ParserTest, Continuation) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
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

TEST_F(ParserTest, Comment) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"# this is a comment\n"
"foo = not # a comment\n"));
  EXPECT_EQ("not # a comment", state.env_["foo"]);
}

TEST_F(ParserTest, Errors) {
  {
    ManifestParser parser(NULL, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("foobar", &err));
    EXPECT_EQ("line 1, col 7: expected '=', got eof", err);
  }

  {
    ManifestParser parser(NULL, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("x 3", &err));
    EXPECT_EQ("line 1, col 3: expected '=', got '3'", err);
  }

  {
    ManifestParser parser(NULL, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("x = 3", &err));
    EXPECT_EQ("line 1, col 6: expected newline, got eof", err);
  }

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("x = 3\ny 2", &err));
    EXPECT_EQ("line 2, col 3: expected '=', got '2'", err);
  }

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("build x: y z\n", &err));
    EXPECT_EQ("line 1, col 10: unknown build rule 'y'", err);
  }

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("build x:: y z\n", &err));
    EXPECT_EQ("line 1, col 9: expected build command name", err);
  }

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("rule cat\n  command = cat ok\n"
                              "build x: cat \\\n :\n",
                              &err));
    EXPECT_EQ("line 4, col 2: expected newline, got ':'", err);
  }

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("rule cat\n"
                              "  foo = bar\n",
                              &err));
    EXPECT_EQ("line 3, col 1: expected 'command =' line", err);
  }

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("rule %foo\n",
                              &err));
    EXPECT_EQ("line 1, col 6: expected rule name, got unknown '%'", err);
  }

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("rule cc\n  command = foo\n  depfile = bar\n"
                              "build a.o b.o: cc c.cc\n",
                              &err));
    EXPECT_EQ("line 4, col 16: dependency files only work with "
              "single-output rules", err);
  }
}

TEST_F(ParserTest, BuildDir) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"builddir = out\n"
"rule cat\n"
"  command = cat $in > $out\n"
"build @bin: cat @a.o\n"
"build @a.o: cat a.cc\n"));
  ASSERT_TRUE(state.LookupNode("out/a.o"));
}

TEST_F(ParserTest, SubNinja) {
  files_["test.ninja"] = "";
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"subninja test.ninja\n"));
  ASSERT_EQ(1, files_read_.size());
  EXPECT_EQ("test.ninja", files_read_[0]);
}

TEST(MakefileParser, Basic) {
  MakefileParser parser;
  string err;
  EXPECT_TRUE(parser.Parse(
"build/ninja.o: ninja.cc ninja.h eval_env.h manifest_parser.h\n",
      &err));
  ASSERT_EQ("", err);
}

