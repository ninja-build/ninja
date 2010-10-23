#include "ninja.h"

#include <gtest/gtest.h>

namespace {

void AssertParse(State* state, const char* input) {
  ManifestParser parser(state);
  string err;
  ASSERT_TRUE(parser.Parse(input, &err)) << err;
  ASSERT_EQ("", err);
}

}

TEST(Parser, Empty) {
  State state;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state, ""));
}

TEST(Parser, Rules) {
  State state;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state,
"rule cat\n"
"command cat @in > $out\n"
"\n"
"rule date\n"
"command date > $out\n"
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
"command ld $extra $with_under -o $out @in\n"
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
"command foo bar \\\n"
"    baz\n"
"\n"
"build a: link c \\\n"
" d e f\n"));

  ASSERT_EQ(1, state.rules_.size());
  Rule* rule = state.rules_.begin()->second;
  EXPECT_EQ("link", rule->name_);
  EXPECT_EQ("foo bar baz", rule->command_.unparsed());
}

TEST(Parser, Errors) {
  State state;

  {
    ManifestParser parser(NULL);
    string err;
    EXPECT_FALSE(parser.Parse("foobar", &err));
    EXPECT_EQ("line 1, col 7: expected '=', got ''", err);
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
    ManifestParser parser(&state);
    string err;
    EXPECT_FALSE(parser.Parse("x = 3\ny 2", &err));
    EXPECT_EQ("line 2, col 3: expected '=', got '2'", err);
  }

  {
    ManifestParser parser(&state);
    string err;
    EXPECT_FALSE(parser.Parse("build x: y z\n", &err));
    EXPECT_EQ("line 1, col 10: unknown build rule 'y'", err);
  }
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

struct StateTestWithBuiltinRules : public testing::Test {
  StateTestWithBuiltinRules() {
    AssertParse(&state_,
"rule cat\n"
"command cat @in > $out\n");
  }

  Node* GetNode(const string& path) {
    return state_.stat_cache()->GetFile(path)->node_;
  }

  State state_;
};

struct BuildTest : public StateTestWithBuiltinRules,
                   public Shell,
                   public StatHelper {
  BuildTest() : builder_(&state_), now_(1) {
    builder_.stat_helper_ = this;
    AssertParse(&state_,
"build cat1: cat in1\n"
"build cat2: cat in1 in2\n"
"build cat12: cat cat1 cat2\n");
  }

  // Mark a path dirty.
  void Dirty(const string& path);
  // Mark dependents of a path dirty.
  void Touch(const string& path);

  // shell override
  virtual bool RunCommand(Edge* edge);

  // StatHelper
  virtual int Stat(const string& path) {
    return now_;
  }

  Builder builder_;
  int now_;
  vector<string> commands_ran_;
};

void BuildTest::Dirty(const string& path) {
  GetNode(path)->MarkDirty();
}

void BuildTest::Touch(const string& path) {
  GetNode(path)->MarkDependentsDirty();
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
  string err;
  EXPECT_TRUE(builder_.Build(this, &err));
  EXPECT_EQ("no work to do", err);
  EXPECT_EQ(0, commands_ran_.size());
}

TEST_F(BuildTest, OneStep) {
  // Given a dirty target with one ready input,
  // we should rebuild the target.
  Dirty("cat1");
  string err;
  ASSERT_TRUE(builder_.AddTarget("cat1", &err));
  EXPECT_TRUE(builder_.Build(this, &err));
  EXPECT_EQ("", err);

  ASSERT_EQ(1, commands_ran_.size());
  EXPECT_EQ("cat in1 > cat1", commands_ran_[0]);
}

TEST_F(BuildTest, OneStep2) {
  // Given a target with one dirty input,
  // we should rebuild the target.
  Dirty("cat1");
  string err;
  EXPECT_TRUE(builder_.AddTarget("cat1", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(this, &err));
  EXPECT_EQ("", err);

  ASSERT_EQ(1, commands_ran_.size());
  EXPECT_EQ("cat in1 > cat1", commands_ran_[0]);
}

TEST_F(BuildTest, TwoStep) {
  // Modifying in1 requires rebuilding both intermediate files
  // and the final file.
  Touch("in1");
  string err;
  EXPECT_TRUE(builder_.AddTarget("cat12", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(this, &err));
  EXPECT_EQ("", err);
  ASSERT_EQ(3, commands_ran_.size());
  EXPECT_EQ("cat in1 > cat1", commands_ran_[0]);
  EXPECT_EQ("cat in1 in2 > cat2", commands_ran_[1]);
  EXPECT_EQ("cat cat1 cat2 > cat12", commands_ran_[2]);

  // Modifying in2 requires rebuilding one intermediate file
  // and the final file.
  Touch("in2");
  ASSERT_TRUE(builder_.AddTarget("cat12", &err));
  EXPECT_TRUE(builder_.Build(this, &err));
  EXPECT_EQ("", err);
  ASSERT_EQ(5, commands_ran_.size());
  EXPECT_EQ("cat in1 in2 > cat2", commands_ran_[3]);
  EXPECT_EQ("cat cat1 cat2 > cat12", commands_ran_[4]);
}

TEST_F(BuildTest, Chain) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build c2: cat c1\n"
"build c3: cat c2\n"
"build c4: cat c3\n"
"build c5: cat c4\n"));

  Touch("c1");  // Will recursively dirty all the way to c5.
  string err;
  EXPECT_TRUE(builder_.AddTarget("c5", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(this, &err));
  EXPECT_EQ("", err);
  ASSERT_EQ(4, commands_ran_.size());

  err.clear();
  commands_ran_.clear();
  EXPECT_FALSE(builder_.AddTarget("c5", &err));
  ASSERT_EQ("target is clean; nothing to do", err);
  EXPECT_TRUE(builder_.Build(this, &err));
  ASSERT_EQ(0, commands_ran_.size());

  Touch("c3");  // Will recursively dirty through to c5.
  err.clear();
  commands_ran_.clear();
  EXPECT_TRUE(builder_.AddTarget("c5", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(this, &err));
  ASSERT_EQ(2, commands_ran_.size());  // 3->4, 4->5
}

TEST_F(BuildTest, MissingInput) {
  string err;
  Dirty("in1");
  EXPECT_FALSE(builder_.AddTarget("cat1", &err));
  EXPECT_EQ("'in1' missing and no known rule to make it", err);
}

struct StatTest : public StateTestWithBuiltinRules,
                  public StatHelper {
  virtual int Stat(const string& path);

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
  ASSERT_EQ(1, stats_.size());
  Edge* edge = out->in_edge_;
  edge->RecomputeDirty(this);
  ASSERT_EQ(2, stats_.size());
  ASSERT_EQ("out", stats_[0]);
  ASSERT_EQ("in",  stats_[1]);
}

TEST_F(StatTest, TwoStep) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n"));

  Node* out = GetNode("out");
  out->file_->Stat(this);
  ASSERT_EQ(1, stats_.size());
  Edge* edge = out->in_edge_;
  edge->RecomputeDirty(this);
  ASSERT_EQ(3, stats_.size());
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
  ASSERT_EQ(1, stats_.size());
  Edge* edge = out->in_edge_;
  edge->RecomputeDirty(this);
  ASSERT_EQ(1 + 6, stats_.size());
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
  ASSERT_EQ(1, stats_.size());
  Edge* edge = out->in_edge_;
  edge->RecomputeDirty(this);
  ASSERT_FALSE(GetNode("in")->dirty_);
  ASSERT_TRUE(GetNode("mid")->dirty_);
  ASSERT_TRUE(GetNode("out")->dirty_);
}
