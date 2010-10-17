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

struct BuildTest : public testing::Test,
                   public Shell {
  BuildTest() {
    LoadManifest();
  }

  void LoadManifest();

  // shell override
  virtual bool RunCommand(Edge* edge);

  State state_;
  int now_;
  vector<string> commands_ran_;
};

void BuildTest::LoadManifest() {
  ManifestParser parser(&state_);
  string err;
  ASSERT_TRUE(parser.Parse(
"rule cat\n"
"command cat @in > $out\n"
"\n"
"build cat1: cat in1\n"
"build cat2: cat in1 in2\n"
"build bin: cat main lib\n",
      &err));
  ASSERT_EQ("", err);
}

bool BuildTest::RunCommand(Edge* edge) {
  commands_ran_.push_back(edge->EvaluateCommand());
  if (edge->rule_->name_ == "cat") {
    for (vector<Node*>::iterator out = edge->outputs_.begin();
         out != edge->outputs_.end(); ++out) {
      (*out)->file_->Touch(now_);
    }
    return true;
  } else {
    printf("unkown command\n");
  }

  return false;
}

TEST_F(BuildTest, NoWork) {
  Builder builder(&state_);
  builder.AddTarget("bin");
  string err;
  EXPECT_TRUE(builder.Build(this, &err));
  EXPECT_EQ("", err);
  EXPECT_EQ(0, commands_ran_.size());
}

TEST_F(BuildTest, OneStep) {
  // Given a dirtytarget with one ready input,
  // we should rebuild the target.
  state_.stat_cache()->GetFile("cat1")->Touch(1);
  Builder builder(&state_);
  builder.AddTarget("cat1");
  string err;
  EXPECT_TRUE(builder.Build(this, &err));
  EXPECT_EQ("", err);

  ASSERT_EQ(1, commands_ran_.size());
  EXPECT_EQ("cat in1 > cat1", commands_ran_[0]);
}
