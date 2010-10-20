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

TEST(Parser, Variables) {
  State state;
  ManifestParser parser(&state);
  string err;
  EXPECT_TRUE(parser.Parse(
"rule link\n"
"command ld $extra -o $out @in\n"
"\n"
"let extra = -pthread\n"
"build a: link b c\n",
      &err));
  EXPECT_EQ("", err);

  ASSERT_EQ(1, state.edges_.size());
  Edge* edge = state.edges_[0];
  EXPECT_EQ("ld -pthread -o a b c", edge->EvaluateCommand());
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
  EXPECT_FALSE(state.GetNode("in1")->dirty());
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
  BuildTest() : builder_(&state_), now_(1) {
    LoadManifest();
  }

  void LoadManifest();
  void Touch(const string& path);

  // shell override
  virtual bool RunCommand(Edge* edge);

  State state_;
  Builder builder_;
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
"build cat12: cat cat1 cat2\n",
      &err));
  ASSERT_EQ("", err);
}

void BuildTest::Touch(const string& path) {
  state_.stat_cache()->GetFile(path)->Touch(1);
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
  builder_.AddTarget("bin");
  string err;
  EXPECT_TRUE(builder_.Build(this, &err));
  EXPECT_EQ("no work to do", err);
  EXPECT_EQ(0, commands_ran_.size());
}

TEST_F(BuildTest, OneStep) {
  // Given a dirty target with one ready input,
  // we should rebuild the target.
  Touch("cat1");
  builder_.AddTarget("cat1");
  string err;
  EXPECT_TRUE(builder_.Build(this, &err));
  EXPECT_EQ("", err);

  ASSERT_EQ(1, commands_ran_.size());
  EXPECT_EQ("cat in1 > cat1", commands_ran_[0]);
}

TEST_F(BuildTest, OneStep2) {
  // Given a target with one dirty input,
  // we should rebuild the target.
  Touch("in1");
  builder_.AddTarget("cat1");
  string err;
  EXPECT_TRUE(builder_.Build(this, &err));
  EXPECT_EQ("", err);

  ASSERT_EQ(1, commands_ran_.size());
  EXPECT_EQ("cat in1 > cat1", commands_ran_[0]);
}

TEST_F(BuildTest, TwoStep) {
  // Touching in1 requires rebuilding both intermediate files
  // and the final file.
  Touch("in1");
  builder_.AddTarget("cat12");
  string err;
  EXPECT_TRUE(builder_.Build(this, &err));
  EXPECT_EQ("", err);
  ASSERT_EQ(3, commands_ran_.size());
  EXPECT_EQ("cat in1 > cat1", commands_ran_[0]);
  EXPECT_EQ("cat in1 in2 > cat2", commands_ran_[1]);
  EXPECT_EQ("cat cat1 cat2 > cat12", commands_ran_[2]);

  // Touching in2 requires rebuilding one intermediate file
  // and the final file.
  Touch("in2");
  builder_.AddTarget("cat12");
  EXPECT_TRUE(builder_.Build(this, &err));
  EXPECT_EQ("", err);
  ASSERT_EQ(5, commands_ran_.size());
  EXPECT_EQ("cat in1 in2 > cat2", commands_ran_[3]);
  EXPECT_EQ("cat cat1 cat2 > cat12", commands_ran_[4]);
}
