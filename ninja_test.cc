#include "ninja.h"

#include <gtest/gtest.h>

struct NinjaTest : public testing::Test {
  NinjaTest() {
    rule_cat_ = state_.AddRule("cat", "cat @in > $out");
  }

  Rule* rule_cat_;
  State state_;
};

TEST_F(NinjaTest, Basic) {
  Edge* edge = state_.AddEdge(rule_cat_);
  state_.AddInOut(edge, Edge::IN, "in1");
  state_.AddInOut(edge, Edge::IN, "in2");
  state_.AddInOut(edge, Edge::OUT, "out");

  EXPECT_EQ("cat in1 in2 > out", edge->EvaluateCommand());

  EXPECT_FALSE(state_.GetNode("in1")->dirty());
  EXPECT_FALSE(state_.GetNode("in2")->dirty());
  EXPECT_FALSE(state_.GetNode("out")->dirty());

  state_.stat_cache()->GetFile("in1")->Touch(1);
  EXPECT_TRUE(state_.GetNode("in1")->dirty());
  EXPECT_FALSE(state_.GetNode("in2")->dirty());
  EXPECT_TRUE(state_.GetNode("out")->dirty());

  Plan plan(&state_);
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

TEST(Parser, Empty) {
  State state;
  ManifestParser parser(&state);
  string err;
  EXPECT_TRUE(parser.Parse("", &err));
  EXPECT_EQ("", err);
}

TEST(Parser, Rule) {
  State state;
  ManifestParser parser(&state);
  string err;
  EXPECT_TRUE(parser.Parse(
      "rule cat\n"
      "command cat @in > $out\n",
      &err));
  EXPECT_EQ("", err);

  ASSERT_EQ(1, state.rules_.size());
  Rule* rule = state.rules_.begin()->second;
  EXPECT_EQ("cat", rule->name_);
  EXPECT_EQ("cat @in > $out", rule->command_.unparsed());
}
