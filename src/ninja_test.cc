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

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

#include <gtest/gtest.h>

#include "build.h"
#include "graph.h"
#include "parsers.h"
#include "test.h"

TEST(State, Basic) {
  State state;
  Rule* rule = new Rule("cat");
  string err;
  EXPECT_TRUE(rule->ParseCommand("cat $in > $out", &err));
  ASSERT_EQ("", err);
  state.AddRule(rule);
  Edge* edge = state.AddEdge(rule);
  state.AddIn(edge, "in1");
  state.AddIn(edge, "in2");
  state.AddOut(edge, "out");

  EXPECT_EQ("cat in1 in2 > out", edge->EvaluateCommand());

  EXPECT_FALSE(state.GetNode("in1")->dirty());
  EXPECT_FALSE(state.GetNode("in2")->dirty());
  EXPECT_FALSE(state.GetNode("out")->dirty());
}

struct TestEnv : public Env {
  virtual string LookupVariable(const string& var) {
    return vars[var];
  }
  map<string, string> vars;
};
TEST(EvalString, PlainText) {
  EvalString str;
  string err;
  EXPECT_TRUE(str.Parse("plain text", &err));
  EXPECT_EQ("", err);
  EXPECT_EQ("plain text", str.Evaluate(NULL));
}
TEST(EvalString, OneVariable) {
  EvalString str;
  string err;
  EXPECT_TRUE(str.Parse("hi $var", &err));
  EXPECT_EQ("", err);
  EXPECT_EQ("hi $var", str.unparsed());
  TestEnv env;
  EXPECT_EQ("hi ", str.Evaluate(&env));
  env.vars["var"] = "there";
  EXPECT_EQ("hi there", str.Evaluate(&env));
}
TEST(EvalString, OneVariableUpperCase) {
  EvalString str;
  string err;
  EXPECT_TRUE(str.Parse("hi $VaR", &err));
  EXPECT_EQ("", err);
  EXPECT_EQ("hi $VaR", str.unparsed());
  TestEnv env;
  EXPECT_EQ("hi ", str.Evaluate(&env));
  env.vars["VaR"] = "there";
  EXPECT_EQ("hi there", str.Evaluate(&env));
}
TEST(EvalString, Error) {
  EvalString str;
  string err;
  size_t err_index;
  EXPECT_FALSE(str.Parse("bad $", &err, &err_index));
  EXPECT_EQ("expected variable after $", err);
  EXPECT_EQ(5u, err_index);
}
TEST(EvalString, CurlyError) {
  EvalString str;
  string err;
  size_t err_index;
  EXPECT_FALSE(str.Parse("bad ${bar", &err, &err_index));
  EXPECT_EQ("expected closing curly after ${", err);
  EXPECT_EQ(9u, err_index);
}
TEST(EvalString, Curlies) {
  EvalString str;
  string err;
  EXPECT_TRUE(str.Parse("foo ${var}baz", &err));
  EXPECT_EQ("", err);
  TestEnv env;
  EXPECT_EQ("foo baz", str.Evaluate(&env));
  env.vars["var"] = "barbar";
  EXPECT_EQ("foo barbarbaz", str.Evaluate(&env));
}
TEST(EvalString, Dollars) {
  EvalString str;
  string err;
  EXPECT_TRUE(str.Parse("foo$$bar$bar", &err));
  ASSERT_EQ("", err);
  TestEnv env;
  env.vars["bar"] = "baz";
  EXPECT_EQ("foo$barbaz", str.Evaluate(&env));
}

struct StatTest : public StateTestWithBuiltinRules,
                  public DiskInterface {
  // DiskInterface implementation.
  virtual int Stat(const string& path);
  virtual bool MakeDir(const string& path) {
    assert(false);
    return false;
  }
  virtual string ReadFile(const string& path, string* err) {
    assert(false);
    return "";
  }
  virtual int RemoveFile(const string& path) {
    assert(false);
    return 0;
  }

  map<string, time_t> mtimes_;
  vector<string> stats_;
};

int StatTest::Stat(const string& path) {
  stats_.push_back(path);
  map<string, time_t>::iterator i = mtimes_.find(path);
  if (i == mtimes_.end())
    return 0;  // File not found.
  return i->second;
}

TEST_F(StatTest, Simple) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat in\n"));

  Node* out = GetNode("out");
  out->file_->Stat(this);
  ASSERT_EQ(1u, stats_.size());
  Edge* edge = out->in_edge_;
  edge->RecomputeDirty(NULL, this, NULL);
  ASSERT_EQ(2u, stats_.size());
  ASSERT_EQ("out", stats_[0]);
  ASSERT_EQ("in",  stats_[1]);
}

TEST_F(StatTest, TwoStep) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n"));

  Node* out = GetNode("out");
  out->file_->Stat(this);
  ASSERT_EQ(1u, stats_.size());
  Edge* edge = out->in_edge_;
  edge->RecomputeDirty(NULL, this, NULL);
  ASSERT_EQ(3u, stats_.size());
  ASSERT_EQ("out", stats_[0]);
  ASSERT_TRUE(GetNode("out")->dirty_);
  ASSERT_EQ("mid",  stats_[1]);
  ASSERT_TRUE(GetNode("mid")->dirty_);
  ASSERT_EQ("in",  stats_[2]);
}

TEST_F(StatTest, Tree) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat mid1 mid2\n"
"build mid1: cat in11 in12\n"
"build mid2: cat in21 in22\n"));

  Node* out = GetNode("out");
  out->file_->Stat(this);
  ASSERT_EQ(1u, stats_.size());
  Edge* edge = out->in_edge_;
  edge->RecomputeDirty(NULL, this, NULL);
  ASSERT_EQ(1u + 6u, stats_.size());
  ASSERT_EQ("mid1", stats_[1]);
  ASSERT_TRUE(GetNode("mid1")->dirty_);
  ASSERT_EQ("in11", stats_[2]);
}

TEST_F(StatTest, Middle) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n"));

  mtimes_["in"] = 1;
  mtimes_["mid"] = 0;  // missing
  mtimes_["out"] = 1;

  Node* out = GetNode("out");
  out->file_->Stat(this);
  ASSERT_EQ(1u, stats_.size());
  Edge* edge = out->in_edge_;
  edge->RecomputeDirty(NULL, this, NULL);
  ASSERT_FALSE(GetNode("in")->dirty_);
  ASSERT_TRUE(GetNode("mid")->dirty_);
  ASSERT_TRUE(GetNode("out")->dirty_);
}


