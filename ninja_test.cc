#include "ninja.h"

#include <gtest/gtest.h>

TEST(Parser, Empty) {
  State state;
  ManifestParser parser(&state);
  string err;
  EXPECT_TRUE(parser.Parse("", &err));
  EXPECT_EQ("", err);
}

TEST(Parser, Rules) {
  State state;
  ManifestParser parser(&state);
  string err;
  EXPECT_TRUE(parser.Parse(
"rule cat\n"
"command cat @in > $out\n"
"\n"
"rule date\n"
"command date > $out\n"
"\n"
"build result: cat in_1.cc in-2.O\n",
      &err));
  EXPECT_EQ("", err);

  ASSERT_EQ(2, state.rules_.size());
  Rule* rule = state.rules_.begin()->second;
  EXPECT_EQ("cat", rule->name_);
  EXPECT_EQ("cat @in > $out", rule->command_.unparsed());
}

TEST(State, Basic) {
  State state;
  Rule* rule = state.AddRule("cat", "cat @in > $out");
  Edge* edge = state.AddEdge(rule);
  state.AddInOut(edge, Edge::IN, "in1");
  state.AddInOut(edge, Edge::IN, "in2");
  state.AddInOut(edge, Edge::OUT, "out");

  EXPECT_EQ("cat in1 in2 > out", edge->EvaluateCommand());

  EXPECT_FALSE(state.GetNode("in1")->dirty());
  EXPECT_FALSE(state.GetNode("in2")->dirty());
  EXPECT_FALSE(state.GetNode("out")->dirty());

  state.stat_cache()->GetFile("in1")->Touch(1);
  EXPECT_TRUE(state.GetNode("in1")->dirty());
  EXPECT_FALSE(state.GetNode("in2")->dirty());
  EXPECT_TRUE(state.GetNode("out")->dirty());

  Plan plan(&state);
  plan.AddTarget("out");
  edge = plan.FindWork();
  ASSERT_TRUE(edge);
  EXPECT_EQ("cat in1 in2 > out", edge->EvaluateCommand());
  ASSERT_FALSE(plan.FindWork());
}

struct TestEnv : public EvalString::Env {
  virtual string Evaluate(const string& var) {
    return vars[var];
  }
  map<string, string> vars;
};
TEST(EvalString, PlainText) {
  EvalString str;
  str.Parse("plain text");
  ASSERT_EQ("plain text", str.Evaluate(NULL));
}
TEST(EvalString, OneVariable) {
  EvalString str;
  ASSERT_TRUE(str.Parse("hi $var"));
  EXPECT_EQ("hi $var", str.unparsed());
  TestEnv env;
  EXPECT_EQ("hi ", str.Evaluate(&env));
  env.vars["$var"] = "there";
  EXPECT_EQ("hi there", str.Evaluate(&env));
}

#include <gmock/gmock.h>
using ::testing::Return;
using ::testing::_;

struct MockShell : public Shell {
  MOCK_METHOD1(RunCommand, bool(const string& command));
};

TEST(Build, OneStep) {
  State state;
  ManifestParser parser(&state);
  string err;
  ASSERT_TRUE(parser.Parse(
"rule cat\n"
"command cat @in > $out\n"
"\n"
"build lib: cat in1 in2\n"
"build bin: cat main lib\n",
      &err));
  ASSERT_EQ("", err);

  {
    MockShell shell;
    Builder builder(&state);
    builder.AddTarget("bin");
    EXPECT_CALL(shell, RunCommand(_))
      .Times(0);
    EXPECT_TRUE(builder.Build(&shell, &err));
    EXPECT_EQ("", err);
  }

  {
    MockShell shell;
    Builder builder(&state);
    state.stat_cache()->GetFile("in1")->Touch(1);
    builder.AddTarget("bin");
    EXPECT_CALL(shell, RunCommand("cat in1 in2 > lib"))
      .WillOnce(Return(true));
    EXPECT_TRUE(builder.Build(&shell, &err));
    EXPECT_EQ("", err);
  }
}
