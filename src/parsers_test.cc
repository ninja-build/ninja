#include "parsers.h"

#include <gtest/gtest.h>

#include "graph.h"
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

  ASSERT_EQ(3, state.rules_.size());
  const Rule* rule = state.rules_.begin()->second;
  EXPECT_EQ("cat", rule->name_);
  EXPECT_EQ("cat $in > $out", rule->command_.unparsed());
}

TEST_F(ParserTest, Variables) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"l = one-letter-test\n"
"rule link\n"
"  command = ld $l $extra $with_under -o $out $in\n"
"\n"
"extra = -pthread\n"
"with_under = -under\n"
"build a: link b c\n"
"nested1 = 1\n"
"nested2 = $nested1/2\n"
"build supernested: link x\n"
"  extra = $nested2/3\n"));

  ASSERT_EQ(2, state.edges_.size());
  Edge* edge = state.edges_[0];
  EXPECT_EQ("ld one-letter-test -pthread -under -o a b c",
            edge->EvaluateCommand());
  EXPECT_EQ("1/2", state.bindings_.LookupVariable("nested2"));

  edge = state.edges_[1];
  EXPECT_EQ("ld one-letter-test 1/2/3 -under -o supernested x",
            edge->EvaluateCommand());
}

TEST_F(ParserTest, VariableScope) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"foo = bar\n"
"rule cmd\n"
"  command = cmd $foo $in $out\n"
"\n"
"build inner: cmd a\n"
"  foo = baz\n"
"build outer: cmd b\n"
"\n"  // Extra newline after build line tickles a regression.
));

  ASSERT_EQ(2, state.edges_.size());
  EXPECT_EQ("cmd baz a inner", state.edges_[0]->EvaluateCommand());
  EXPECT_EQ("cmd bar b outer", state.edges_[1]->EvaluateCommand());
}

TEST_F(ParserTest, Continuation) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule link\n"
"  command = foo bar \\\n"
"    baz\n"
"\n"
"build a: link c \\\n"
" d e f\n"));

  ASSERT_EQ(2, state.rules_.size());
  const Rule* rule = state.rules_.begin()->second;
  EXPECT_EQ("link", rule->name_);
  EXPECT_EQ("foo bar baz", rule->command_.unparsed());
}

TEST_F(ParserTest, Backslash) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"foo = bar\\baz\n"
"foo2 = bar\\ baz\n"
));
  EXPECT_EQ("bar\\baz", state.bindings_.LookupVariable("foo"));
  EXPECT_EQ("bar\\ baz", state.bindings_.LookupVariable("foo2"));
}

TEST_F(ParserTest, Comment) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"# this is a comment\n"
"foo = not # a comment\n"));
  EXPECT_EQ("not # a comment", state.bindings_.LookupVariable("foo"));
}

TEST_F(ParserTest, CanonicalizeFile) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule cat\n"
"  command = cat $in > $out\n"
"build out: cat in/1 in//2\n"
"build in/1: cat\n"
"build in/2: cat\n"));

  EXPECT_TRUE(state.LookupNode("in/1"));
  EXPECT_TRUE(state.LookupNode("in/2"));
  EXPECT_FALSE(state.LookupNode("in//1"));
  EXPECT_FALSE(state.LookupNode("in//2"));
}

TEST_F(ParserTest, PathVariables) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule cat\n"
"  command = cat $in > $out\n"
"dir = out\n"
"build $dir/exe: cat src\n"));

  EXPECT_FALSE(state.LookupNode("$dir/exe"));
  EXPECT_TRUE(state.LookupNode("out/exe"));
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
    EXPECT_EQ("line 1, col 9: expected build command name, got ':'", err);
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
    EXPECT_FALSE(parser.Parse("rule cat\n",
                              &err));
    EXPECT_EQ("line 2, col 1: expected 'command =' line", err);
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

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("rule cc\n  command = foo\n  othervar = bar\n",
                              &err));
    EXPECT_EQ("line 4, col 0: unexpected variable 'othervar'", err);
  }

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("rule cc\n  command = foo\n"
                              "build $: cc bar.cc\n",
                              &err));
    EXPECT_EQ("line 4, col 1: expected variable after $", err);
  }
}

TEST_F(ParserTest, SubNinja) {
  files_["test.ninja"] =
    "var = inner\n"
    "build $builddir/inner: varref\n";
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"builddir = some_dir/\n"
"rule varref\n"
"  command = varref $var\n"
"var = outer\n"
"build $builddir/outer: varref\n"
"subninja test.ninja\n"
"build $builddir/outer2: varref\n"));
  ASSERT_EQ(1, files_read_.size());

  EXPECT_EQ("test.ninja", files_read_[0]);
  EXPECT_TRUE(state.LookupNode("some_dir/outer"));
  // Verify our builddir setting is inherited.
  EXPECT_TRUE(state.LookupNode("some_dir/inner"));

  ASSERT_EQ(3, state.edges_.size());
  EXPECT_EQ("varref outer", state.edges_[0]->EvaluateCommand());
  EXPECT_EQ("varref inner", state.edges_[1]->EvaluateCommand());
  EXPECT_EQ("varref outer", state.edges_[2]->EvaluateCommand());
}

TEST_F(ParserTest, Include) {
  files_["include.ninja"] = "var = inner\n";
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"var = outer\n"
"include include.ninja\n"));

  ASSERT_EQ(1, files_read_.size());
  EXPECT_EQ("include.ninja", files_read_[0]);
  EXPECT_EQ("inner", state.bindings_.LookupVariable("var"));
}

TEST_F(ParserTest, Implicit) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule cat\n  command = cat $in > $out\n"
"build foo: cat bar | baz\n"));

  Edge* edge = state.LookupNode("foo")->in_edge_;
  ASSERT_TRUE(edge->is_implicit(1));
}

TEST_F(ParserTest, OrderOnly) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule cat\n  command = cat $in > $out\n"
"build foo: cat bar || baz\n"));

  Edge* edge = state.LookupNode("foo")->in_edge_;
  ASSERT_TRUE(edge->is_order_only(1));
}

TEST(MakefileParser, Basic) {
  MakefileParser parser;
  string err;
  EXPECT_TRUE(parser.Parse(
"build/ninja.o: ninja.cc ninja.h eval_env.h manifest_parser.h\n",
      &err));
  ASSERT_EQ("", err);
}

TEST(MakefileParser, EarlyNewlineAndWhitespace) {
  MakefileParser parser;
  string err;
  EXPECT_TRUE(parser.Parse(
" \\\n"
"  out: in\n",
      &err));
  ASSERT_EQ("", err);
}

