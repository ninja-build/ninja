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

#include "parsers.h"

#include <gtest/gtest.h>

#include "graph.h"
#include "state.h"

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
      *err = "No such file or directory";  // Match strerror() for ENOENT.
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

  ASSERT_EQ(3u, state.rules_.size());
  const Rule* rule = state.rules_.begin()->second;
  EXPECT_EQ("cat", rule->name());
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

  ASSERT_EQ(2u, state.edges_.size());
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

  ASSERT_EQ(2u, state.edges_.size());
  EXPECT_EQ("cmd baz a inner", state.edges_[0]->EvaluateCommand());
  EXPECT_EQ("cmd bar b outer", state.edges_[1]->EvaluateCommand());
}

TEST_F(ParserTest, Continuation) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule link\n"
"  command = foo bar $\n"
"    baz\n"
"\n"
"build a: link c $\n"
" d e f\n"));

  ASSERT_EQ(2u, state.rules_.size());
  const Rule* rule = state.rules_.begin()->second;
  EXPECT_EQ("link", rule->name());
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

TEST_F(ParserTest, Dollars) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule foo\n"
"  command = ${out}bar$$baz$$$\n"
"blah\n"
"x = $$dollar\n"
"build $x: foo y\n"
));
  EXPECT_EQ("$dollar", state.bindings_.LookupVariable("x"));
  EXPECT_EQ("$dollarbar$baz$blah", state.edges_[0]->EvaluateCommand());
}

TEST_F(ParserTest, EscapeSpaces) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule has$ spaces\n"
"  command = something\n"
"build foo$ bar: has$ spaces $$one two$$$ three\n"
));
  EXPECT_TRUE(state.LookupNode("foo bar"));
  EXPECT_EQ(state.edges_[0]->outputs_[0]->path(), "foo bar");
  EXPECT_EQ(state.edges_[0]->inputs_[0]->path(), "$one");
  EXPECT_EQ(state.edges_[0]->inputs_[1]->path(), "two$ three");
  EXPECT_EQ(state.edges_[0]->EvaluateCommand(), "something");
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

TEST_F(ParserTest, CanonicalizePaths) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule cat\n"
"  command = cat $in > $out\n"
"build ./out.o: cat ./bar/baz/../foo.cc\n"));

  EXPECT_FALSE(state.LookupNode("./out.o"));
  EXPECT_TRUE(state.LookupNode("out.o"));
  EXPECT_FALSE(state.LookupNode("./bar/baz/../foo.cc"));
  EXPECT_TRUE(state.LookupNode("bar/foo.cc"));
}

TEST_F(ParserTest, ReservedWords) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule build\n"
"  command = rule run $out\n"
"build subninja: build include default foo.cc\n"
"default subninja\n"));
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
    EXPECT_FALSE(parser.Parse("x = $", &err));
    EXPECT_EQ("line 1, col 3: unexpected eof", err);
  }

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("x = $\n $[\n", &err));
    EXPECT_EQ("line 2, col 3: expected variable after $", err);
  }

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("x = a$\n b$\n $\n", &err));
    EXPECT_EQ("line 4, col 1: expected newline, got eof", err);
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
                              "build x: cat $\n :\n",
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
    EXPECT_FALSE(parser.Parse("rule cat\n  command = ${fafsd\n  foo = bar\n",
                              &err));
    EXPECT_EQ("line 2, col 20: expected closing curly after ${", err);
  }


  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("rule cat\n  command = cat\nbuild $: cat foo\n",
                              &err));
    // XXX EXPECT_EQ("line 3, col 7: expected variable after $", err);
    EXPECT_EQ("line 4, col 1: expected variable after $", err);
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
    EXPECT_FALSE(parser.Parse("rule cc\n  command = foo\n  othervar = bar\n",
                              &err));
    EXPECT_EQ("line 3, col 3: unexpected variable 'othervar'", err);
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

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("default\n",
                              &err));
    EXPECT_EQ("line 1, col 8: expected target name, got newline", err);
  }

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("default nonexistent\n",
                              &err));
    EXPECT_EQ("line 1, col 9: unknown target 'nonexistent'", err);
  }

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("rule r\n  command = r\n"
                              "build b: r\n"
                              "default b:\n",
                              &err));
    EXPECT_EQ("line 4, col 10: expected newline, got ':'", err);
  }

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("default $a\n", &err));
    EXPECT_EQ("line 1, col 9: empty path", err);
  }

  {
    State state;
    ManifestParser parser(&state, NULL);
    string err;
    EXPECT_FALSE(parser.Parse("rule r\n"
                              "  command = r\n"
                              "build $a: r $c\n", &err));
    // XXX the line number is wrong; we should evaluate paths in ParseEdge
    // as we see them, not after we've read them all!
    EXPECT_EQ("line 4, col 1: empty path", err);
  }
}

TEST_F(ParserTest, MultipleOutputs)
{
  State state;
  ManifestParser parser(&state, NULL);
  string err;
  EXPECT_TRUE(parser.Parse("rule cc\n  command = foo\n  depfile = bar\n"
                            "build a.o b.o: cc c.cc\n",
                            &err));
  EXPECT_EQ("", err);
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
  ASSERT_EQ(1u, files_read_.size());

  EXPECT_EQ("test.ninja", files_read_[0]);
  EXPECT_TRUE(state.LookupNode("some_dir/outer"));
  // Verify our builddir setting is inherited.
  EXPECT_TRUE(state.LookupNode("some_dir/inner"));

  ASSERT_EQ(3u, state.edges_.size());
  EXPECT_EQ("varref outer", state.edges_[0]->EvaluateCommand());
  EXPECT_EQ("varref inner", state.edges_[1]->EvaluateCommand());
  EXPECT_EQ("varref outer", state.edges_[2]->EvaluateCommand());
}

TEST_F(ParserTest, MissingSubNinja) {
  ManifestParser parser(&state, this);
  string err;
  EXPECT_FALSE(parser.Parse("subninja foo.ninja\n", &err));
  EXPECT_EQ("line 1, col 10: loading foo.ninja: No such file or directory",
            err);
}

TEST_F(ParserTest, Include) {
  files_["include.ninja"] = "var = inner\n";
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"var = outer\n"
"include include.ninja\n"));

  ASSERT_EQ(1u, files_read_.size());
  EXPECT_EQ("include.ninja", files_read_[0]);
  EXPECT_EQ("inner", state.bindings_.LookupVariable("var"));
}

TEST_F(ParserTest, Implicit) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule cat\n  command = cat $in > $out\n"
"build foo: cat bar | baz\n"));

  Edge* edge = state.LookupNode("foo")->in_edge();
  ASSERT_TRUE(edge->is_implicit(1));
}

TEST_F(ParserTest, OrderOnly) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule cat\n  command = cat $in > $out\n"
"build foo: cat bar || baz\n"));

  Edge* edge = state.LookupNode("foo")->in_edge();
  ASSERT_TRUE(edge->is_order_only(1));
}

TEST_F(ParserTest, DefaultDefault) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule cat\n  command = cat $in > $out\n"
"build a: cat foo\n"
"build b: cat foo\n"
"build c: cat foo\n"
"build d: cat foo\n"));

  string err;
  EXPECT_EQ(4u, state.DefaultNodes(&err).size());
  EXPECT_EQ("", err);
}

TEST_F(ParserTest, DefaultStatements) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"rule cat\n  command = cat $in > $out\n"
"build a: cat foo\n"
"build b: cat foo\n"
"build c: cat foo\n"
"build d: cat foo\n"
"third = c\n"
"default a b\n"
"default $third\n"));

  string err;
  std::vector<Node*> nodes = state.DefaultNodes(&err);
  EXPECT_EQ("", err);
  ASSERT_EQ(3u, nodes.size());
  EXPECT_EQ("a", nodes[0]->path());
  EXPECT_EQ("b", nodes[1]->path());
  EXPECT_EQ("c", nodes[2]->path());
}
