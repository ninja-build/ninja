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

#include "build.h"

#include <assert.h>

#include "build_log.h"
#include "deps_log.h"
#include "graph.h"
#include "test.h"

/// Fixture for tests involving Plan.
// Though Plan doesn't use State, it's useful to have one around
// to create Nodes and Edges.
struct PlanTest : public StateTestWithBuiltinRules {
  Plan plan_;

  /// Because FindWork does not return Edges in any sort of predictable order,
  // provide a means to get available Edges in order and in a format which is
  // easy to write tests around.
  void FindWorkSorted(deque<Edge*>* ret, int count) {
    struct CompareEdgesByOutput {
      static bool cmp(const Edge* a, const Edge* b) {
        return a->outputs_[0]->path() < b->outputs_[0]->path();
      }
    };

    for (int i = 0; i < count; ++i) {
      ASSERT_TRUE(plan_.more_to_do());
      Edge* edge = plan_.FindWork();
      ASSERT_TRUE(edge);
      ret->push_back(edge);
    }
    ASSERT_FALSE(plan_.FindWork());
    sort(ret->begin(), ret->end(), CompareEdgesByOutput::cmp);
  }

  void TestPoolWithDepthOne(const char *test_case);
};

TEST_F(PlanTest, Basic) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n"));
  GetNode("mid")->MarkDirty();
  GetNode("out")->MarkDirty();
  string err;
  EXPECT_TRUE(plan_.AddTarget(GetNode("out"), &err));
  ASSERT_EQ("", err);
  ASSERT_TRUE(plan_.more_to_do());

  Edge* edge = plan_.FindWork();
  ASSERT_TRUE(edge);
  ASSERT_EQ("in",  edge->inputs_[0]->path());
  ASSERT_EQ("mid", edge->outputs_[0]->path());

  ASSERT_FALSE(plan_.FindWork());

  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);
  ASSERT_EQ("mid", edge->inputs_[0]->path());
  ASSERT_EQ("out", edge->outputs_[0]->path());

  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  ASSERT_FALSE(plan_.more_to_do());
  edge = plan_.FindWork();
  ASSERT_EQ(0, edge);
}

// Test that two outputs from one rule can be handled as inputs to the next.
TEST_F(PlanTest, DoubleOutputDirect) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat mid1 mid2\n"
"build mid1 mid2: cat in\n"));
  GetNode("mid1")->MarkDirty();
  GetNode("mid2")->MarkDirty();
  GetNode("out")->MarkDirty();

  string err;
  EXPECT_TRUE(plan_.AddTarget(GetNode("out"), &err));
  ASSERT_EQ("", err);
  ASSERT_TRUE(plan_.more_to_do());

  Edge* edge;
  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat in
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat mid1 mid2
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_FALSE(edge);  // done
}

// Test that two outputs from one rule can eventually be routed to another.
TEST_F(PlanTest, DoubleOutputIndirect) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat b1 b2\n"
"build b1: cat a1\n"
"build b2: cat a2\n"
"build a1 a2: cat in\n"));
  GetNode("a1")->MarkDirty();
  GetNode("a2")->MarkDirty();
  GetNode("b1")->MarkDirty();
  GetNode("b2")->MarkDirty();
  GetNode("out")->MarkDirty();
  string err;
  EXPECT_TRUE(plan_.AddTarget(GetNode("out"), &err));
  ASSERT_EQ("", err);
  ASSERT_TRUE(plan_.more_to_do());

  Edge* edge;
  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat in
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat a1
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat a2
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat b1 b2
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_FALSE(edge);  // done
}

// Test that two edges from one output can both execute.
TEST_F(PlanTest, DoubleDependent) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat a1 a2\n"
"build a1: cat mid\n"
"build a2: cat mid\n"
"build mid: cat in\n"));
  GetNode("mid")->MarkDirty();
  GetNode("a1")->MarkDirty();
  GetNode("a2")->MarkDirty();
  GetNode("out")->MarkDirty();

  string err;
  EXPECT_TRUE(plan_.AddTarget(GetNode("out"), &err));
  ASSERT_EQ("", err);
  ASSERT_TRUE(plan_.more_to_do());

  Edge* edge;
  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat in
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat mid
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat mid
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat a1 a2
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_FALSE(edge);  // done
}

TEST_F(PlanTest, DependencyCycle) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n"
"build in: cat pre\n"
"build pre: cat out\n"));
  GetNode("out")->MarkDirty();
  GetNode("mid")->MarkDirty();
  GetNode("in")->MarkDirty();
  GetNode("pre")->MarkDirty();

  string err;
  EXPECT_FALSE(plan_.AddTarget(GetNode("out"), &err));
  ASSERT_EQ("dependency cycle: out -> mid -> in -> pre -> out", err);
}

TEST_F(PlanTest, CycleInEdgesButNotInNodes1) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build a b: cat a\n"));
  EXPECT_FALSE(plan_.AddTarget(GetNode("b"), &err));
  ASSERT_EQ("dependency cycle: a -> a", err);
}

TEST_F(PlanTest, CycleInEdgesButNotInNodes2) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build b a: cat a\n"));
  EXPECT_FALSE(plan_.AddTarget(GetNode("b"), &err));
  ASSERT_EQ("dependency cycle: a -> a", err);
}

TEST_F(PlanTest, CycleInEdgesButNotInNodes3) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build a b: cat c\n"
"build c: cat a\n"));
  EXPECT_FALSE(plan_.AddTarget(GetNode("b"), &err));
  ASSERT_EQ("dependency cycle: c -> a -> c", err);
}

TEST_F(PlanTest, CycleInEdgesButNotInNodes4) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build d: cat c\n"
"build c: cat b\n"
"build b: cat a\n"
"build a e: cat d\n"
"build f: cat e\n"));
  EXPECT_FALSE(plan_.AddTarget(GetNode("f"), &err));
  ASSERT_EQ("dependency cycle: d -> c -> b -> a -> d", err);
}

void PlanTest::TestPoolWithDepthOne(const char* test_case) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_, test_case));
  GetNode("out1")->MarkDirty();
  GetNode("out2")->MarkDirty();
  string err;
  EXPECT_TRUE(plan_.AddTarget(GetNode("out1"), &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(plan_.AddTarget(GetNode("out2"), &err));
  ASSERT_EQ("", err);
  ASSERT_TRUE(plan_.more_to_do());

  Edge* edge = plan_.FindWork();
  ASSERT_TRUE(edge);
  ASSERT_EQ("in",  edge->inputs_[0]->path());
  ASSERT_EQ("out1", edge->outputs_[0]->path());

  // This will be false since poolcat is serialized
  ASSERT_FALSE(plan_.FindWork());

  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);
  ASSERT_EQ("in", edge->inputs_[0]->path());
  ASSERT_EQ("out2", edge->outputs_[0]->path());

  ASSERT_FALSE(plan_.FindWork());

  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  ASSERT_FALSE(plan_.more_to_do());
  edge = plan_.FindWork();
  ASSERT_EQ(0, edge);
}

TEST_F(PlanTest, PoolWithDepthOne) {
  TestPoolWithDepthOne(
"pool foobar\n"
"  depth = 1\n"
"rule poolcat\n"
"  command = cat $in > $out\n"
"  pool = foobar\n"
"build out1: poolcat in\n"
"build out2: poolcat in\n");
}

TEST_F(PlanTest, ConsolePool) {
  TestPoolWithDepthOne(
"rule poolcat\n"
"  command = cat $in > $out\n"
"  pool = console\n"
"build out1: poolcat in\n"
"build out2: poolcat in\n");
}

TEST_F(PlanTest, PoolsWithDepthTwo) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"pool foobar\n"
"  depth = 2\n"
"pool bazbin\n"
"  depth = 2\n"
"rule foocat\n"
"  command = cat $in > $out\n"
"  pool = foobar\n"
"rule bazcat\n"
"  command = cat $in > $out\n"
"  pool = bazbin\n"
"build out1: foocat in\n"
"build out2: foocat in\n"
"build out3: foocat in\n"
"build outb1: bazcat in\n"
"build outb2: bazcat in\n"
"build outb3: bazcat in\n"
"  pool =\n"
"build allTheThings: cat out1 out2 out3 outb1 outb2 outb3\n"
));
  // Mark all the out* nodes dirty
  for (int i = 0; i < 3; ++i) {
    GetNode("out" + string(1, '1' + static_cast<char>(i)))->MarkDirty();
    GetNode("outb" + string(1, '1' + static_cast<char>(i)))->MarkDirty();
  }
  GetNode("allTheThings")->MarkDirty();

  string err;
  EXPECT_TRUE(plan_.AddTarget(GetNode("allTheThings"), &err));
  ASSERT_EQ("", err);

  deque<Edge*> edges;
  FindWorkSorted(&edges, 5);

  for (int i = 0; i < 4; ++i) {
    Edge *edge = edges[i];
    ASSERT_EQ("in",  edge->inputs_[0]->path());
    string base_name(i < 2 ? "out" : "outb");
    ASSERT_EQ(base_name + string(1, '1' + (i % 2)), edge->outputs_[0]->path());
  }

  // outb3 is exempt because it has an empty pool
  Edge* edge = edges[4];
  ASSERT_TRUE(edge);
  ASSERT_EQ("in",  edge->inputs_[0]->path());
  ASSERT_EQ("outb3", edge->outputs_[0]->path());

  // finish out1
  plan_.EdgeFinished(edges.front(), Plan::kEdgeSucceeded);
  edges.pop_front();

  // out3 should be available
  Edge* out3 = plan_.FindWork();
  ASSERT_TRUE(out3);
  ASSERT_EQ("in",  out3->inputs_[0]->path());
  ASSERT_EQ("out3", out3->outputs_[0]->path());

  ASSERT_FALSE(plan_.FindWork());

  plan_.EdgeFinished(out3, Plan::kEdgeSucceeded);

  ASSERT_FALSE(plan_.FindWork());

  for (deque<Edge*>::iterator it = edges.begin(); it != edges.end(); ++it) {
    plan_.EdgeFinished(*it, Plan::kEdgeSucceeded);
  }

  Edge* last = plan_.FindWork();
  ASSERT_TRUE(last);
  ASSERT_EQ("allTheThings", last->outputs_[0]->path());

  plan_.EdgeFinished(last, Plan::kEdgeSucceeded);

  ASSERT_FALSE(plan_.more_to_do());
  ASSERT_FALSE(plan_.FindWork());
}

TEST_F(PlanTest, PoolWithRedundantEdges) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "pool compile\n"
    "  depth = 1\n"
    "rule gen_foo\n"
    "  command = touch foo.cpp\n"
    "rule gen_bar\n"
    "  command = touch bar.cpp\n"
    "rule echo\n"
    "  command = echo $out > $out\n"
    "build foo.cpp.obj: echo foo.cpp || foo.cpp\n"
    "  pool = compile\n"
    "build bar.cpp.obj: echo bar.cpp || bar.cpp\n"
    "  pool = compile\n"
    "build libfoo.a: echo foo.cpp.obj bar.cpp.obj\n"
    "build foo.cpp: gen_foo\n"
    "build bar.cpp: gen_bar\n"
    "build all: phony libfoo.a\n"));
  GetNode("foo.cpp")->MarkDirty();
  GetNode("foo.cpp.obj")->MarkDirty();
  GetNode("bar.cpp")->MarkDirty();
  GetNode("bar.cpp.obj")->MarkDirty();
  GetNode("libfoo.a")->MarkDirty();
  GetNode("all")->MarkDirty();
  string err;
  EXPECT_TRUE(plan_.AddTarget(GetNode("all"), &err));
  ASSERT_EQ("", err);
  ASSERT_TRUE(plan_.more_to_do());

  Edge* edge = NULL;

  deque<Edge*> initial_edges;
  FindWorkSorted(&initial_edges, 2);

  edge = initial_edges[1];  // Foo first
  ASSERT_EQ("foo.cpp", edge->outputs_[0]->path());
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);
  ASSERT_FALSE(plan_.FindWork());
  ASSERT_EQ("foo.cpp", edge->inputs_[0]->path());
  ASSERT_EQ("foo.cpp", edge->inputs_[1]->path());
  ASSERT_EQ("foo.cpp.obj", edge->outputs_[0]->path());
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = initial_edges[0];  // Now for bar
  ASSERT_EQ("bar.cpp", edge->outputs_[0]->path());
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);
  ASSERT_FALSE(plan_.FindWork());
  ASSERT_EQ("bar.cpp", edge->inputs_[0]->path());
  ASSERT_EQ("bar.cpp", edge->inputs_[1]->path());
  ASSERT_EQ("bar.cpp.obj", edge->outputs_[0]->path());
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);
  ASSERT_FALSE(plan_.FindWork());
  ASSERT_EQ("foo.cpp.obj", edge->inputs_[0]->path());
  ASSERT_EQ("bar.cpp.obj", edge->inputs_[1]->path());
  ASSERT_EQ("libfoo.a", edge->outputs_[0]->path());
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);
  ASSERT_FALSE(plan_.FindWork());
  ASSERT_EQ("libfoo.a", edge->inputs_[0]->path());
  ASSERT_EQ("all", edge->outputs_[0]->path());
  plan_.EdgeFinished(edge, Plan::kEdgeSucceeded);

  edge = plan_.FindWork();
  ASSERT_FALSE(edge);
  ASSERT_FALSE(plan_.more_to_do());
}

TEST_F(PlanTest, PoolWithFailingEdge) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "pool foobar\n"
    "  depth = 1\n"
    "rule poolcat\n"
    "  command = cat $in > $out\n"
    "  pool = foobar\n"
    "build out1: poolcat in\n"
    "build out2: poolcat in\n"));
  GetNode("out1")->MarkDirty();
  GetNode("out2")->MarkDirty();
  string err;
  EXPECT_TRUE(plan_.AddTarget(GetNode("out1"), &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(plan_.AddTarget(GetNode("out2"), &err));
  ASSERT_EQ("", err);
  ASSERT_TRUE(plan_.more_to_do());

  Edge* edge = plan_.FindWork();
  ASSERT_TRUE(edge);
  ASSERT_EQ("in",  edge->inputs_[0]->path());
  ASSERT_EQ("out1", edge->outputs_[0]->path());

  // This will be false since poolcat is serialized
  ASSERT_FALSE(plan_.FindWork());

  plan_.EdgeFinished(edge, Plan::kEdgeFailed);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);
  ASSERT_EQ("in", edge->inputs_[0]->path());
  ASSERT_EQ("out2", edge->outputs_[0]->path());

  ASSERT_FALSE(plan_.FindWork());

  plan_.EdgeFinished(edge, Plan::kEdgeFailed);

  ASSERT_TRUE(plan_.more_to_do()); // Jobs have failed
  edge = plan_.FindWork();
  ASSERT_EQ(0, edge);
}

/// Fake implementation of CommandRunner, useful for tests.
struct FakeCommandRunner : public CommandRunner {
  explicit FakeCommandRunner(VirtualFileSystem* fs) :
      last_command_(NULL), fs_(fs) {}

  // CommandRunner impl
  virtual bool CanRunMore();
  virtual bool StartCommand(Edge* edge);
  virtual bool WaitForCommand(Result* result);
  virtual vector<Edge*> GetActiveEdges();
  virtual void Abort();

  vector<string> commands_ran_;
  Edge* last_command_;
  VirtualFileSystem* fs_;
};

struct BuildTest : public StateTestWithBuiltinRules, public BuildLogUser {
  BuildTest() : config_(MakeConfig()), command_runner_(&fs_),
                builder_(&state_, config_, NULL, NULL, &fs_),
                status_(config_) {
  }

  virtual void SetUp() {
    StateTestWithBuiltinRules::SetUp();

    builder_.command_runner_.reset(&command_runner_);
    AssertParse(&state_,
"build cat1: cat in1\n"
"build cat2: cat in1 in2\n"
"build cat12: cat cat1 cat2\n");

    fs_.Create("in1", "");
    fs_.Create("in2", "");
  }

  ~BuildTest() {
    builder_.command_runner_.release();
  }

  virtual bool IsPathDead(StringPiece s) const { return false; }

  /// Rebuild target in the 'working tree' (fs_).
  /// State of command_runner_ and logs contents (if specified) ARE MODIFIED.
  /// Handy to check for NOOP builds, and higher-level rebuild tests.
  void RebuildTarget(const string& target, const char* manifest,
                     const char* log_path = NULL, const char* deps_path = NULL,
                     State* state = NULL);

  // Mark a path dirty.
  void Dirty(const string& path);

  BuildConfig MakeConfig() {
    BuildConfig config;
    config.verbosity = BuildConfig::QUIET;
    return config;
  }

  BuildConfig config_;
  FakeCommandRunner command_runner_;
  VirtualFileSystem fs_;
  Builder builder_;

  BuildStatus status_;
};

void BuildTest::RebuildTarget(const string& target, const char* manifest,
                              const char* log_path, const char* deps_path,
                              State* state) {
  State local_state, *pstate = &local_state;
  if (state)
    pstate = state;
  ASSERT_NO_FATAL_FAILURE(AddCatRule(pstate));
  AssertParse(pstate, manifest);

  string err;
  BuildLog build_log, *pbuild_log = NULL;
  if (log_path) {
    ASSERT_TRUE(build_log.Load(log_path, &err));
    ASSERT_TRUE(build_log.OpenForWrite(log_path, *this, &err));
    ASSERT_EQ("", err);
    pbuild_log = &build_log;
  }

  DepsLog deps_log, *pdeps_log = NULL;
  if (deps_path) {
    ASSERT_TRUE(deps_log.Load(deps_path, pstate, &err));
    ASSERT_TRUE(deps_log.OpenForWrite(deps_path, &err));
    ASSERT_EQ("", err);
    pdeps_log = &deps_log;
  }

  Builder builder(pstate, config_, pbuild_log, pdeps_log, &fs_);
  EXPECT_TRUE(builder.AddTarget(target, &err));

  command_runner_.commands_ran_.clear();
  builder.command_runner_.reset(&command_runner_);
  if (!builder.AlreadyUpToDate()) {
    bool build_res = builder.Build(&err);
    EXPECT_TRUE(build_res);
  }
  builder.command_runner_.release();
}

bool FakeCommandRunner::CanRunMore() {
  // Only run one at a time.
  return last_command_ == NULL;
}

bool FakeCommandRunner::StartCommand(Edge* edge) {
  assert(!last_command_);
  commands_ran_.push_back(edge->EvaluateCommand());
  if (edge->rule().name() == "cat"  ||
      edge->rule().name() == "cat_rsp" ||
      edge->rule().name() == "cat_rsp_out" ||
      edge->rule().name() == "cc" ||
      edge->rule().name() == "touch" ||
      edge->rule().name() == "touch-interrupt") {
    for (vector<Node*>::iterator out = edge->outputs_.begin();
         out != edge->outputs_.end(); ++out) {
      fs_->Create((*out)->path(), "");
    }
  } else if (edge->rule().name() == "true" ||
             edge->rule().name() == "fail" ||
             edge->rule().name() == "interrupt" ||
             edge->rule().name() == "console") {
    // Don't do anything.
  } else {
    printf("unknown command\n");
    return false;
  }

  last_command_ = edge;
  return true;
}

bool FakeCommandRunner::WaitForCommand(Result* result) {
  if (!last_command_)
    return false;

  Edge* edge = last_command_;
  result->edge = edge;

  if (edge->rule().name() == "interrupt" ||
      edge->rule().name() == "touch-interrupt") {
    result->status = ExitInterrupted;
    return true;
  }

  if (edge->rule().name() == "console") {
    if (edge->use_console())
      result->status = ExitSuccess;
    else
      result->status = ExitFailure;
    last_command_ = NULL;
    return true;
  }

  if (edge->rule().name() == "fail")
    result->status = ExitFailure;
  else
    result->status = ExitSuccess;
  last_command_ = NULL;
  return true;
}

vector<Edge*> FakeCommandRunner::GetActiveEdges() {
  vector<Edge*> edges;
  if (last_command_)
    edges.push_back(last_command_);
  return edges;
}

void FakeCommandRunner::Abort() {
  last_command_ = NULL;
}

void BuildTest::Dirty(const string& path) {
  Node* node = GetNode(path);
  node->MarkDirty();

  // If it's an input file, mark that we've already stat()ed it and
  // it's missing.
  if (!node->in_edge())
    node->MarkMissing();
}

TEST_F(BuildTest, NoWork) {
  string err;
  EXPECT_TRUE(builder_.AlreadyUpToDate());
}

TEST_F(BuildTest, OneStep) {
  // Given a dirty target with one ready input,
  // we should rebuild the target.
  Dirty("cat1");
  string err;
  EXPECT_TRUE(builder_.AddTarget("cat1", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);

  ASSERT_EQ(1u, command_runner_.commands_ran_.size());
  EXPECT_EQ("cat in1 > cat1", command_runner_.commands_ran_[0]);
}

TEST_F(BuildTest, OneStep2) {
  // Given a target with one dirty input,
  // we should rebuild the target.
  Dirty("cat1");
  string err;
  EXPECT_TRUE(builder_.AddTarget("cat1", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);

  ASSERT_EQ(1u, command_runner_.commands_ran_.size());
  EXPECT_EQ("cat in1 > cat1", command_runner_.commands_ran_[0]);
}

TEST_F(BuildTest, TwoStep) {
  string err;
  EXPECT_TRUE(builder_.AddTarget("cat12", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);
  ASSERT_EQ(3u, command_runner_.commands_ran_.size());
  // Depending on how the pointers work out, we could've ran
  // the first two commands in either order.
  EXPECT_TRUE((command_runner_.commands_ran_[0] == "cat in1 > cat1" &&
               command_runner_.commands_ran_[1] == "cat in1 in2 > cat2") ||
              (command_runner_.commands_ran_[1] == "cat in1 > cat1" &&
               command_runner_.commands_ran_[0] == "cat in1 in2 > cat2"));

  EXPECT_EQ("cat cat1 cat2 > cat12", command_runner_.commands_ran_[2]);

  fs_.Tick();

  // Modifying in2 requires rebuilding one intermediate file
  // and the final file.
  fs_.Create("in2", "");
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("cat12", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(5u, command_runner_.commands_ran_.size());
  EXPECT_EQ("cat in1 in2 > cat2", command_runner_.commands_ran_[3]);
  EXPECT_EQ("cat cat1 cat2 > cat12", command_runner_.commands_ran_[4]);
}

TEST_F(BuildTest, TwoOutputs) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule touch\n"
"  command = touch $out\n"
"build out1 out2: touch in.txt\n"));

  fs_.Create("in.txt", "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("out1", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());
  EXPECT_EQ("touch out1 out2", command_runner_.commands_ran_[0]);
}

TEST_F(BuildTest, ImplicitOutput) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule touch\n"
"  command = touch $out $out.imp\n"
"build out | out.imp: touch in.txt\n"));
  fs_.Create("in.txt", "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("out.imp", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());
  EXPECT_EQ("touch out out.imp", command_runner_.commands_ran_[0]);
}

// Test case from
//   https://github.com/ninja-build/ninja/issues/148
TEST_F(BuildTest, MultiOutIn) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule touch\n"
"  command = touch $out\n"
"build in1 otherfile: touch in\n"
"build out: touch in | in1\n"));

  fs_.Create("in", "");
  fs_.Tick();
  fs_.Create("in1", "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("out", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);
}

TEST_F(BuildTest, Chain) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build c2: cat c1\n"
"build c3: cat c2\n"
"build c4: cat c3\n"
"build c5: cat c4\n"));

  fs_.Create("c1", "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("c5", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);
  ASSERT_EQ(4u, command_runner_.commands_ran_.size());

  err.clear();
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("c5", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.AlreadyUpToDate());

  fs_.Tick();

  fs_.Create("c3", "");
  err.clear();
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("c5", &err));
  ASSERT_EQ("", err);
  EXPECT_FALSE(builder_.AlreadyUpToDate());
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(2u, command_runner_.commands_ran_.size());  // 3->4, 4->5
}

TEST_F(BuildTest, MissingInput) {
  // Input is referenced by build file, but no rule for it.
  string err;
  Dirty("in1");
  EXPECT_FALSE(builder_.AddTarget("cat1", &err));
  EXPECT_EQ("'in1', needed by 'cat1', missing and no known rule to make it",
            err);
}

TEST_F(BuildTest, MissingTarget) {
  // Target is not referenced by build file.
  string err;
  EXPECT_FALSE(builder_.AddTarget("meow", &err));
  EXPECT_EQ("unknown target: 'meow'", err);
}

TEST_F(BuildTest, MakeDirs) {
  string err;

#ifdef _WIN32
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
                                      "build subdir\\dir2\\file: cat in1\n"));
#else
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
                                      "build subdir/dir2/file: cat in1\n"));
#endif
  EXPECT_TRUE(builder_.AddTarget("subdir/dir2/file", &err));

  EXPECT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(2u, fs_.directories_made_.size());
  EXPECT_EQ("subdir", fs_.directories_made_[0]);
  EXPECT_EQ("subdir/dir2", fs_.directories_made_[1]);
}

TEST_F(BuildTest, DepFileMissing) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n  command = cc $in\n  depfile = $out.d\n"
"build fo$ o.o: cc foo.c\n"));
  fs_.Create("foo.c", "");

  EXPECT_TRUE(builder_.AddTarget("fo o.o", &err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, fs_.files_read_.size());
  EXPECT_EQ("fo o.o.d", fs_.files_read_[0]);
}

TEST_F(BuildTest, DepFileOK) {
  string err;
  int orig_edges = state_.edges_.size();
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n  command = cc $in\n  depfile = $out.d\n"
"build foo.o: cc foo.c\n"));
  Edge* edge = state_.edges_.back();

  fs_.Create("foo.c", "");
  GetNode("bar.h")->MarkDirty();  // Mark bar.h as missing.
  fs_.Create("foo.o.d", "foo.o: blah.h bar.h\n");
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, fs_.files_read_.size());
  EXPECT_EQ("foo.o.d", fs_.files_read_[0]);

  // Expect three new edges: one generating foo.o, and two more from
  // loading the depfile.
  ASSERT_EQ(orig_edges + 3, (int)state_.edges_.size());
  // Expect our edge to now have three inputs: foo.c and two headers.
  ASSERT_EQ(3u, edge->inputs_.size());

  // Expect the command line we generate to only use the original input.
  ASSERT_EQ("cc foo.c", edge->EvaluateCommand());
}

TEST_F(BuildTest, DepFileParseError) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n  command = cc $in\n  depfile = $out.d\n"
"build foo.o: cc foo.c\n"));
  fs_.Create("foo.c", "");
  fs_.Create("foo.o.d", "randomtext\n");
  EXPECT_FALSE(builder_.AddTarget("foo.o", &err));
  EXPECT_EQ("foo.o.d: expected ':' in depfile", err);
}

TEST_F(BuildTest, EncounterReadyTwice) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule touch\n"
"  command = touch $out\n"
"build c: touch\n"
"build b: touch || c\n"
"build a: touch | b || c\n"));

  vector<Edge*> c_out = GetNode("c")->out_edges();
  ASSERT_EQ(2u, c_out.size());
  EXPECT_EQ("b", c_out[0]->outputs_[0]->path());
  EXPECT_EQ("a", c_out[1]->outputs_[0]->path());

  fs_.Create("b", "");
  EXPECT_TRUE(builder_.AddTarget("a", &err));
  ASSERT_EQ("", err);

  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(2u, command_runner_.commands_ran_.size());
}

TEST_F(BuildTest, OrderOnlyDeps) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n  command = cc $in\n  depfile = $out.d\n"
"build foo.o: cc foo.c || otherfile\n"));
  Edge* edge = state_.edges_.back();

  fs_.Create("foo.c", "");
  fs_.Create("otherfile", "");
  fs_.Create("foo.o.d", "foo.o: blah.h bar.h\n");
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  ASSERT_EQ("", err);

  // One explicit, two implicit, one order only.
  ASSERT_EQ(4u, edge->inputs_.size());
  EXPECT_EQ(2, edge->implicit_deps_);
  EXPECT_EQ(1, edge->order_only_deps_);
  // Verify the inputs are in the order we expect
  // (explicit then implicit then orderonly).
  EXPECT_EQ("foo.c", edge->inputs_[0]->path());
  EXPECT_EQ("blah.h", edge->inputs_[1]->path());
  EXPECT_EQ("bar.h", edge->inputs_[2]->path());
  EXPECT_EQ("otherfile", edge->inputs_[3]->path());

  // Expect the command line we generate to only use the original input.
  ASSERT_EQ("cc foo.c", edge->EvaluateCommand());

  // explicit dep dirty, expect a rebuild.
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());

  fs_.Tick();

  // Recreate the depfile, as it should have been deleted by the build.
  fs_.Create("foo.o.d", "foo.o: blah.h bar.h\n");

  // implicit dep dirty, expect a rebuild.
  fs_.Create("blah.h", "");
  fs_.Create("bar.h", "");
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());

  fs_.Tick();

  // Recreate the depfile, as it should have been deleted by the build.
  fs_.Create("foo.o.d", "foo.o: blah.h bar.h\n");

  // order only dep dirty, no rebuild.
  fs_.Create("otherfile", "");
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_EQ("", err);
  EXPECT_TRUE(builder_.AlreadyUpToDate());

  // implicit dep missing, expect rebuild.
  fs_.RemoveFile("bar.h");
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());
}

TEST_F(BuildTest, RebuildOrderOnlyDeps) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n  command = cc $in\n"
"rule true\n  command = true\n"
"build oo.h: cc oo.h.in\n"
"build foo.o: cc foo.c || oo.h\n"));

  fs_.Create("foo.c", "");
  fs_.Create("oo.h.in", "");

  // foo.o and order-only dep dirty, build both.
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(2u, command_runner_.commands_ran_.size());

  // all clean, no rebuild.
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_EQ("", err);
  EXPECT_TRUE(builder_.AlreadyUpToDate());

  // order-only dep missing, build it only.
  fs_.RemoveFile("oo.h");
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());
  ASSERT_EQ("cc oo.h.in", command_runner_.commands_ran_[0]);

  fs_.Tick();

  // order-only dep dirty, build it only.
  fs_.Create("oo.h.in", "");
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());
  ASSERT_EQ("cc oo.h.in", command_runner_.commands_ran_[0]);
}

#ifdef _WIN32
TEST_F(BuildTest, DepFileCanonicalize) {
  string err;
  int orig_edges = state_.edges_.size();
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n  command = cc $in\n  depfile = $out.d\n"
"build gen/stuff\\things/foo.o: cc x\\y/z\\foo.c\n"));
  Edge* edge = state_.edges_.back();

  fs_.Create("x/y/z/foo.c", "");
  GetNode("bar.h")->MarkDirty();  // Mark bar.h as missing.
  // Note, different slashes from manifest.
  fs_.Create("gen/stuff\\things/foo.o.d",
             "gen\\stuff\\things\\foo.o: blah.h bar.h\n");
  EXPECT_TRUE(builder_.AddTarget("gen/stuff/things/foo.o", &err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, fs_.files_read_.size());
  // The depfile path does not get Canonicalize as it seems unnecessary.
  EXPECT_EQ("gen/stuff\\things/foo.o.d", fs_.files_read_[0]);

  // Expect three new edges: one generating foo.o, and two more from
  // loading the depfile.
  ASSERT_EQ(orig_edges + 3, (int)state_.edges_.size());
  // Expect our edge to now have three inputs: foo.c and two headers.
  ASSERT_EQ(3u, edge->inputs_.size());

  // Expect the command line we generate to only use the original input, and
  // using the slashes from the manifest.
  ASSERT_EQ("cc x\\y/z\\foo.c", edge->EvaluateCommand());
}
#endif

TEST_F(BuildTest, Phony) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat bar.cc\n"
"build all: phony out\n"));
  fs_.Create("bar.cc", "");

  EXPECT_TRUE(builder_.AddTarget("all", &err));
  ASSERT_EQ("", err);

  // Only one command to run, because phony runs no command.
  EXPECT_FALSE(builder_.AlreadyUpToDate());
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());
}

TEST_F(BuildTest, PhonyNoWork) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat bar.cc\n"
"build all: phony out\n"));
  fs_.Create("bar.cc", "");
  fs_.Create("out", "");

  EXPECT_TRUE(builder_.AddTarget("all", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.AlreadyUpToDate());
}

TEST_F(BuildTest, Fail) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule fail\n"
"  command = fail\n"
"build out1: fail\n"));

  string err;
  EXPECT_TRUE(builder_.AddTarget("out1", &err));
  ASSERT_EQ("", err);

  EXPECT_FALSE(builder_.Build(&err));
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());
  ASSERT_EQ("subcommand failed", err);
}

TEST_F(BuildTest, SwallowFailures) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule fail\n"
"  command = fail\n"
"build out1: fail\n"
"build out2: fail\n"
"build out3: fail\n"
"build all: phony out1 out2 out3\n"));

  // Swallow two failures, die on the third.
  config_.failures_allowed = 3;

  string err;
  EXPECT_TRUE(builder_.AddTarget("all", &err));
  ASSERT_EQ("", err);

  EXPECT_FALSE(builder_.Build(&err));
  ASSERT_EQ(3u, command_runner_.commands_ran_.size());
  ASSERT_EQ("subcommands failed", err);
}

TEST_F(BuildTest, SwallowFailuresLimit) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule fail\n"
"  command = fail\n"
"build out1: fail\n"
"build out2: fail\n"
"build out3: fail\n"
"build final: cat out1 out2 out3\n"));

  // Swallow ten failures; we should stop before building final.
  config_.failures_allowed = 11;

  string err;
  EXPECT_TRUE(builder_.AddTarget("final", &err));
  ASSERT_EQ("", err);

  EXPECT_FALSE(builder_.Build(&err));
  ASSERT_EQ(3u, command_runner_.commands_ran_.size());
  ASSERT_EQ("cannot make progress due to previous errors", err);
}

TEST_F(BuildTest, SwallowFailuresPool) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"pool failpool\n"
"  depth = 1\n"
"rule fail\n"
"  command = fail\n"
"  pool = failpool\n"
"build out1: fail\n"
"build out2: fail\n"
"build out3: fail\n"
"build final: cat out1 out2 out3\n"));

  // Swallow ten failures; we should stop before building final.
  config_.failures_allowed = 11;

  string err;
  EXPECT_TRUE(builder_.AddTarget("final", &err));
  ASSERT_EQ("", err);

  EXPECT_FALSE(builder_.Build(&err));
  ASSERT_EQ(3u, command_runner_.commands_ran_.size());
  ASSERT_EQ("cannot make progress due to previous errors", err);
}

TEST_F(BuildTest, PoolEdgesReadyButNotWanted) {
  fs_.Create("x", "");

  const char* manifest =
    "pool some_pool\n"
    "  depth = 4\n"
    "rule touch\n"
    "  command = touch $out\n"
    "  pool = some_pool\n"
    "rule cc\n"
    "  command = touch grit\n"
    "\n"
    "build B.d.stamp: cc | x\n"
    "build C.stamp: touch B.d.stamp\n"
    "build final.stamp: touch || C.stamp\n";

  RebuildTarget("final.stamp", manifest);

  fs_.RemoveFile("B.d.stamp");

  State save_state;
  RebuildTarget("final.stamp", manifest, NULL, NULL, &save_state);
  EXPECT_GE(save_state.LookupPool("some_pool")->current_use(), 0);
}

struct BuildWithLogTest : public BuildTest {
  BuildWithLogTest() {
    builder_.SetBuildLog(&build_log_);
  }

  BuildLog build_log_;
};

TEST_F(BuildWithLogTest, NotInLogButOnDisk) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n"
"  command = cc\n"
"build out1: cc in\n"));

  // Create input/output that would be considered up to date when
  // not considering the command line hash.
  fs_.Create("in", "");
  fs_.Create("out1", "");
  string err;

  // Because it's not in the log, it should not be up-to-date until
  // we build again.
  EXPECT_TRUE(builder_.AddTarget("out1", &err));
  EXPECT_FALSE(builder_.AlreadyUpToDate());

  command_runner_.commands_ran_.clear();
  state_.Reset();

  EXPECT_TRUE(builder_.AddTarget("out1", &err));
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_TRUE(builder_.AlreadyUpToDate());
}

TEST_F(BuildWithLogTest, RestatTest) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule true\n"
"  command = true\n"
"  restat = 1\n"
"rule cc\n"
"  command = cc\n"
"  restat = 1\n"
"build out1: cc in\n"
"build out2: true out1\n"
"build out3: cat out2\n"));

  fs_.Create("out1", "");
  fs_.Create("out2", "");
  fs_.Create("out3", "");

  fs_.Tick();

  fs_.Create("in", "");

  // Do a pre-build so that there's commands in the log for the outputs,
  // otherwise, the lack of an entry in the build log will cause out3 to rebuild
  // regardless of restat.
  string err;
  EXPECT_TRUE(builder_.AddTarget("out3", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  EXPECT_EQ("[3/3]", builder_.status_->FormatProgressStatus("[%s/%t]",
      BuildStatus::kEdgeStarted));
  command_runner_.commands_ran_.clear();
  state_.Reset();

  fs_.Tick();

  fs_.Create("in", "");
  // "cc" touches out1, so we should build out2.  But because "true" does not
  // touch out2, we should cancel the build of out3.
  EXPECT_TRUE(builder_.AddTarget("out3", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(2u, command_runner_.commands_ran_.size());

  // If we run again, it should be a no-op, because the build log has recorded
  // that we've already built out2 with an input timestamp of 2 (from out1).
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("out3", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.AlreadyUpToDate());

  fs_.Tick();

  fs_.Create("in", "");

  // The build log entry should not, however, prevent us from rebuilding out2
  // if out1 changes.
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("out3", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(2u, command_runner_.commands_ran_.size());
}

TEST_F(BuildWithLogTest, RestatMissingFile) {
  // If a restat rule doesn't create its output, and the output didn't
  // exist before the rule was run, consider that behavior equivalent
  // to a rule that doesn't modify its existent output file.

  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule true\n"
"  command = true\n"
"  restat = 1\n"
"rule cc\n"
"  command = cc\n"
"build out1: true in\n"
"build out2: cc out1\n"));

  fs_.Create("in", "");
  fs_.Create("out2", "");

  // Do a pre-build so that there's commands in the log for the outputs,
  // otherwise, the lack of an entry in the build log will cause out2 to rebuild
  // regardless of restat.
  string err;
  EXPECT_TRUE(builder_.AddTarget("out2", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  command_runner_.commands_ran_.clear();
  state_.Reset();

  fs_.Tick();
  fs_.Create("in", "");
  fs_.Create("out2", "");

  // Run a build, expect only the first command to run.
  // It doesn't touch its output (due to being the "true" command), so
  // we shouldn't run the dependent build.
  EXPECT_TRUE(builder_.AddTarget("out2", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());
}

TEST_F(BuildWithLogTest, RestatSingleDependentOutputDirty) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "rule true\n"
    "  command = true\n"
    "  restat = 1\n"
    "rule touch\n"
    "  command = touch\n"
    "build out1: true in\n"
    "build out2 out3: touch out1\n"
    "build out4: touch out2\n"
    ));

  // Create the necessary files
  fs_.Create("in", "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("out4", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(3u, command_runner_.commands_ran_.size());

  fs_.Tick();
  fs_.Create("in", "");
  fs_.RemoveFile("out3");

  // Since "in" is missing, out1 will be built. Since "out3" is missing,
  // out2 and out3 will be built even though "in" is not touched when built.
  // Then, since out2 is rebuilt, out4 should be rebuilt -- the restat on the
  // "true" rule should not lead to the "touch" edge writing out2 and out3 being
  // cleard.
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("out4", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(3u, command_runner_.commands_ran_.size());
}

// Test scenario, in which an input file is removed, but output isn't changed
// https://github.com/ninja-build/ninja/issues/295
TEST_F(BuildWithLogTest, RestatMissingInput) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "rule true\n"
    "  command = true\n"
    "  depfile = $out.d\n"
    "  restat = 1\n"
    "rule cc\n"
    "  command = cc\n"
    "build out1: true in\n"
    "build out2: cc out1\n"));

  // Create all necessary files
  fs_.Create("in", "");

  // The implicit dependencies and the depfile itself
  // are newer than the output
  TimeStamp restat_mtime = fs_.Tick();
  fs_.Create("out1.d", "out1: will.be.deleted restat.file\n");
  fs_.Create("will.be.deleted", "");
  fs_.Create("restat.file", "");

  // Run the build, out1 and out2 get built
  string err;
  EXPECT_TRUE(builder_.AddTarget("out2", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(2u, command_runner_.commands_ran_.size());

  // See that an entry in the logfile is created, capturing
  // the right mtime
  BuildLog::LogEntry* log_entry = build_log_.LookupByOutput("out1");
  ASSERT_TRUE(NULL != log_entry);
  ASSERT_EQ(restat_mtime, log_entry->restat_mtime);

  // Now remove a file, referenced from depfile, so that target becomes
  // dirty, but the output does not change
  fs_.RemoveFile("will.be.deleted");

  // Trigger the build again - only out1 gets built
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("out2", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());

  // Check that the logfile entry remains correctly set
  log_entry = build_log_.LookupByOutput("out1");
  ASSERT_TRUE(NULL != log_entry);
  ASSERT_EQ(restat_mtime, log_entry->restat_mtime);
}

struct BuildDryRun : public BuildWithLogTest {
  BuildDryRun() {
    config_.dry_run = true;
  }
};

TEST_F(BuildDryRun, AllCommandsShown) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule true\n"
"  command = true\n"
"  restat = 1\n"
"rule cc\n"
"  command = cc\n"
"  restat = 1\n"
"build out1: cc in\n"
"build out2: true out1\n"
"build out3: cat out2\n"));

  fs_.Create("out1", "");
  fs_.Create("out2", "");
  fs_.Create("out3", "");

  fs_.Tick();

  fs_.Create("in", "");

  // "cc" touches out1, so we should build out2.  But because "true" does not
  // touch out2, we should cancel the build of out3.
  string err;
  EXPECT_TRUE(builder_.AddTarget("out3", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(3u, command_runner_.commands_ran_.size());
}

// Test that RSP files are created when & where appropriate and deleted after
// successful execution.
TEST_F(BuildTest, RspFileSuccess)
{
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "rule cat_rsp\n"
    "  command = cat $rspfile > $out\n"
    "  rspfile = $rspfile\n"
    "  rspfile_content = $long_command\n"
    "rule cat_rsp_out\n"
    "  command = cat $rspfile > $out\n"
    "  rspfile = $out.rsp\n"
    "  rspfile_content = $long_command\n"
    "build out1: cat in\n"
    "build out2: cat_rsp in\n"
    "  rspfile = out 2.rsp\n"
    "  long_command = Some very long command\n"
    "build out$ 3: cat_rsp_out in\n"
    "  long_command = Some very long command\n"));

  fs_.Create("out1", "");
  fs_.Create("out2", "");
  fs_.Create("out 3", "");

  fs_.Tick();

  fs_.Create("in", "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("out1", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.AddTarget("out2", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.AddTarget("out 3", &err));
  ASSERT_EQ("", err);

  size_t files_created = fs_.files_created_.size();
  size_t files_removed = fs_.files_removed_.size();

  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(3u, command_runner_.commands_ran_.size());

  // The RSP files were created
  ASSERT_EQ(files_created + 2, fs_.files_created_.size());
  ASSERT_EQ(1u, fs_.files_created_.count("out 2.rsp"));
  ASSERT_EQ(1u, fs_.files_created_.count("out 3.rsp"));

  // The RSP files were removed
  ASSERT_EQ(files_removed + 2, fs_.files_removed_.size());
  ASSERT_EQ(1u, fs_.files_removed_.count("out 2.rsp"));
  ASSERT_EQ(1u, fs_.files_removed_.count("out 3.rsp"));
}

// Test that RSP file is created but not removed for commands, which fail
TEST_F(BuildTest, RspFileFailure) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "rule fail\n"
    "  command = fail\n"
    "  rspfile = $rspfile\n"
    "  rspfile_content = $long_command\n"
    "build out: fail in\n"
    "  rspfile = out.rsp\n"
    "  long_command = Another very long command\n"));

  fs_.Create("out", "");
  fs_.Tick();
  fs_.Create("in", "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("out", &err));
  ASSERT_EQ("", err);

  size_t files_created = fs_.files_created_.size();
  size_t files_removed = fs_.files_removed_.size();

  EXPECT_FALSE(builder_.Build(&err));
  ASSERT_EQ("subcommand failed", err);
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());

  // The RSP file was created
  ASSERT_EQ(files_created + 1, fs_.files_created_.size());
  ASSERT_EQ(1u, fs_.files_created_.count("out.rsp"));

  // The RSP file was NOT removed
  ASSERT_EQ(files_removed, fs_.files_removed_.size());
  ASSERT_EQ(0u, fs_.files_removed_.count("out.rsp"));

  // The RSP file contains what it should
  ASSERT_EQ("Another very long command", fs_.files_["out.rsp"].contents);
}

// Test that contents of the RSP file behaves like a regular part of
// command line, i.e. triggers a rebuild if changed
TEST_F(BuildWithLogTest, RspFileCmdLineChange) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "rule cat_rsp\n"
    "  command = cat $rspfile > $out\n"
    "  rspfile = $rspfile\n"
    "  rspfile_content = $long_command\n"
    "build out: cat_rsp in\n"
    "  rspfile = out.rsp\n"
    "  long_command = Original very long command\n"));

  fs_.Create("out", "");
  fs_.Tick();
  fs_.Create("in", "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("out", &err));
  ASSERT_EQ("", err);

  // 1. Build for the 1st time (-> populate log)
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());

  // 2. Build again (no change)
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("out", &err));
  EXPECT_EQ("", err);
  ASSERT_TRUE(builder_.AlreadyUpToDate());

  // 3. Alter the entry in the logfile
  // (to simulate a change in the command line between 2 builds)
  BuildLog::LogEntry* log_entry = build_log_.LookupByOutput("out");
  ASSERT_TRUE(NULL != log_entry);
  ASSERT_NO_FATAL_FAILURE(AssertHash(
        "cat out.rsp > out;rspfile=Original very long command",
        log_entry->command_hash));
  log_entry->command_hash++;  // Change the command hash to something else.
  // Now expect the target to be rebuilt
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("out", &err));
  EXPECT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ(1u, command_runner_.commands_ran_.size());
}

TEST_F(BuildTest, InterruptCleanup) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule interrupt\n"
"  command = interrupt\n"
"rule touch-interrupt\n"
"  command = touch-interrupt\n"
"build out1: interrupt in1\n"
"build out2: touch-interrupt in2\n"));

  fs_.Create("out1", "");
  fs_.Create("out2", "");
  fs_.Tick();
  fs_.Create("in1", "");
  fs_.Create("in2", "");

  // An untouched output of an interrupted command should be retained.
  string err;
  EXPECT_TRUE(builder_.AddTarget("out1", &err));
  EXPECT_EQ("", err);
  EXPECT_FALSE(builder_.Build(&err));
  EXPECT_EQ("interrupted by user", err);
  builder_.Cleanup();
  EXPECT_GT(fs_.Stat("out1", &err), 0);
  err = "";

  // A touched output of an interrupted command should be deleted.
  EXPECT_TRUE(builder_.AddTarget("out2", &err));
  EXPECT_EQ("", err);
  EXPECT_FALSE(builder_.Build(&err));
  EXPECT_EQ("interrupted by user", err);
  builder_.Cleanup();
  EXPECT_EQ(0, fs_.Stat("out2", &err));
}

TEST_F(BuildTest, StatFailureAbortsBuild) {
  const string kTooLongToStat(400, 'i');
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
("build " + kTooLongToStat + ": cat " + kTooLongToStat + "\n").c_str()));
  // Also cyclic, for good measure.

  // This simulates a stat failure:
  fs_.files_[kTooLongToStat].mtime = -1;
  fs_.files_[kTooLongToStat].stat_error = "stat failed";

  string err;
  EXPECT_FALSE(builder_.AddTarget(kTooLongToStat, &err));
  EXPECT_EQ("stat failed", err);
}

TEST_F(BuildTest, PhonyWithNoInputs) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build nonexistent: phony\n"
"build out1: cat || nonexistent\n"
"build out2: cat nonexistent\n"));
  fs_.Create("out1", "");
  fs_.Create("out2", "");

  // out1 should be up to date even though its input is dirty, because its
  // order-only dependency has nothing to do.
  string err;
  EXPECT_TRUE(builder_.AddTarget("out1", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.AlreadyUpToDate());

  // out2 should still be out of date though, because its input is dirty.
  err.clear();
  command_runner_.commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("out2", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());
}

TEST_F(BuildTest, DepsGccWithEmptyDepfileErrorsOut) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n"
"  command = cc\n"
"  deps = gcc\n"
"build out: cc\n"));
  Dirty("out");

  string err;
  EXPECT_TRUE(builder_.AddTarget("out", &err));
  ASSERT_EQ("", err);
  EXPECT_FALSE(builder_.AlreadyUpToDate());

  EXPECT_FALSE(builder_.Build(&err));
  ASSERT_EQ("subcommand failed", err);
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());
}

TEST_F(BuildTest, StatusFormatElapsed) {
  status_.BuildStarted();
  // Before any task is done, the elapsed time must be zero.
  EXPECT_EQ("[%/e0.000]",
            status_.FormatProgressStatus("[%%/e%e]",
                BuildStatus::kEdgeStarted));
}

TEST_F(BuildTest, StatusFormatReplacePlaceholder) {
  EXPECT_EQ("[%/s0/t0/r0/u0/f0]",
            status_.FormatProgressStatus("[%%/s%s/t%t/r%r/u%u/f%f]",
                BuildStatus::kEdgeStarted));
}

TEST_F(BuildTest, FailedDepsParse) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build bad_deps.o: cat in1\n"
"  deps = gcc\n"
"  depfile = in1.d\n"));

  string err;
  EXPECT_TRUE(builder_.AddTarget("bad_deps.o", &err));
  ASSERT_EQ("", err);

  // These deps will fail to parse, as they should only have one
  // path to the left of the colon.
  fs_.Create("in1.d", "AAA BBB");

  EXPECT_FALSE(builder_.Build(&err));
  EXPECT_EQ("subcommand failed", err);
}

/// Tests of builds involving deps logs necessarily must span
/// multiple builds.  We reuse methods on BuildTest but not the
/// builder_ it sets up, because we want pristine objects for
/// each build.
struct BuildWithDepsLogTest : public BuildTest {
  BuildWithDepsLogTest() {}

  virtual void SetUp() {
    BuildTest::SetUp();

    temp_dir_.CreateAndEnter("BuildWithDepsLogTest");
  }

  virtual void TearDown() {
    temp_dir_.Cleanup();
  }

  ScopedTempDir temp_dir_;

  /// Shadow parent class builder_ so we don't accidentally use it.
  void* builder_;
};

/// Run a straightforwad build where the deps log is used.
TEST_F(BuildWithDepsLogTest, Straightforward) {
  string err;
  // Note: in1 was created by the superclass SetUp().
  const char* manifest =
      "build out: cat in1\n"
      "  deps = gcc\n"
      "  depfile = in1.d\n";
  {
    State state;
    ASSERT_NO_FATAL_FAILURE(AddCatRule(&state));
    ASSERT_NO_FATAL_FAILURE(AssertParse(&state, manifest));

    // Run the build once, everything should be ok.
    DepsLog deps_log;
    ASSERT_TRUE(deps_log.OpenForWrite("ninja_deps", &err));
    ASSERT_EQ("", err);

    Builder builder(&state, config_, NULL, &deps_log, &fs_);
    builder.command_runner_.reset(&command_runner_);
    EXPECT_TRUE(builder.AddTarget("out", &err));
    ASSERT_EQ("", err);
    fs_.Create("in1.d", "out: in2");
    EXPECT_TRUE(builder.Build(&err));
    EXPECT_EQ("", err);

    // The deps file should have been removed.
    EXPECT_EQ(0, fs_.Stat("in1.d", &err));
    // Recreate it for the next step.
    fs_.Create("in1.d", "out: in2");
    deps_log.Close();
    builder.command_runner_.release();
  }

  {
    State state;
    ASSERT_NO_FATAL_FAILURE(AddCatRule(&state));
    ASSERT_NO_FATAL_FAILURE(AssertParse(&state, manifest));

    // Touch the file only mentioned in the deps.
    fs_.Tick();
    fs_.Create("in2", "");

    // Run the build again.
    DepsLog deps_log;
    ASSERT_TRUE(deps_log.Load("ninja_deps", &state, &err));
    ASSERT_TRUE(deps_log.OpenForWrite("ninja_deps", &err));

    Builder builder(&state, config_, NULL, &deps_log, &fs_);
    builder.command_runner_.reset(&command_runner_);
    command_runner_.commands_ran_.clear();
    EXPECT_TRUE(builder.AddTarget("out", &err));
    ASSERT_EQ("", err);
    EXPECT_TRUE(builder.Build(&err));
    EXPECT_EQ("", err);

    // We should have rebuilt the output due to in2 being
    // out of date.
    EXPECT_EQ(1u, command_runner_.commands_ran_.size());

    builder.command_runner_.release();
  }
}

/// Verify that obsolete dependency info causes a rebuild.
/// 1) Run a successful build where everything has time t, record deps.
/// 2) Move input/output to time t+1 -- despite files in alignment,
///    should still need to rebuild due to deps at older time.
TEST_F(BuildWithDepsLogTest, ObsoleteDeps) {
  string err;
  // Note: in1 was created by the superclass SetUp().
  const char* manifest =
      "build out: cat in1\n"
      "  deps = gcc\n"
      "  depfile = in1.d\n";
  {
    // Run an ordinary build that gathers dependencies.
    fs_.Create("in1", "");
    fs_.Create("in1.d", "out: ");

    State state;
    ASSERT_NO_FATAL_FAILURE(AddCatRule(&state));
    ASSERT_NO_FATAL_FAILURE(AssertParse(&state, manifest));

    // Run the build once, everything should be ok.
    DepsLog deps_log;
    ASSERT_TRUE(deps_log.OpenForWrite("ninja_deps", &err));
    ASSERT_EQ("", err);

    Builder builder(&state, config_, NULL, &deps_log, &fs_);
    builder.command_runner_.reset(&command_runner_);
    EXPECT_TRUE(builder.AddTarget("out", &err));
    ASSERT_EQ("", err);
    EXPECT_TRUE(builder.Build(&err));
    EXPECT_EQ("", err);

    deps_log.Close();
    builder.command_runner_.release();
  }

  // Push all files one tick forward so that only the deps are out
  // of date.
  fs_.Tick();
  fs_.Create("in1", "");
  fs_.Create("out", "");

  // The deps file should have been removed, so no need to timestamp it.
  EXPECT_EQ(0, fs_.Stat("in1.d", &err));

  {
    State state;
    ASSERT_NO_FATAL_FAILURE(AddCatRule(&state));
    ASSERT_NO_FATAL_FAILURE(AssertParse(&state, manifest));

    DepsLog deps_log;
    ASSERT_TRUE(deps_log.Load("ninja_deps", &state, &err));
    ASSERT_TRUE(deps_log.OpenForWrite("ninja_deps", &err));

    Builder builder(&state, config_, NULL, &deps_log, &fs_);
    builder.command_runner_.reset(&command_runner_);
    command_runner_.commands_ran_.clear();
    EXPECT_TRUE(builder.AddTarget("out", &err));
    ASSERT_EQ("", err);

    // Recreate the deps file here because the build expects them to exist.
    fs_.Create("in1.d", "out: ");

    EXPECT_TRUE(builder.Build(&err));
    EXPECT_EQ("", err);

    // We should have rebuilt the output due to the deps being
    // out of date.
    EXPECT_EQ(1u, command_runner_.commands_ran_.size());

    builder.command_runner_.release();
  }
}

TEST_F(BuildWithDepsLogTest, DepsIgnoredInDryRun) {
  const char* manifest =
      "build out: cat in1\n"
      "  deps = gcc\n"
      "  depfile = in1.d\n";

  fs_.Create("out", "");
  fs_.Tick();
  fs_.Create("in1", "");

  State state;
  ASSERT_NO_FATAL_FAILURE(AddCatRule(&state));
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state, manifest));

  // The deps log is NULL in dry runs.
  config_.dry_run = true;
  Builder builder(&state, config_, NULL, NULL, &fs_);
  builder.command_runner_.reset(&command_runner_);
  command_runner_.commands_ran_.clear();

  string err;
  EXPECT_TRUE(builder.AddTarget("out", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder.Build(&err));
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());

  builder.command_runner_.release();
}

/// Check that a restat rule generating a header cancels compilations correctly.
TEST_F(BuildTest, RestatDepfileDependency) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule true\n"
"  command = true\n"  // Would be "write if out-of-date" in reality.
"  restat = 1\n"
"build header.h: true header.in\n"
"build out: cat in1\n"
"  depfile = in1.d\n"));

  fs_.Create("header.h", "");
  fs_.Create("in1.d", "out: header.h");
  fs_.Tick();
  fs_.Create("header.in", "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("out", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);
}

/// Check that a restat rule generating a header cancels compilations correctly,
/// depslog case.
TEST_F(BuildWithDepsLogTest, RestatDepfileDependencyDepsLog) {
  string err;
  // Note: in1 was created by the superclass SetUp().
  const char* manifest =
      "rule true\n"
      "  command = true\n"  // Would be "write if out-of-date" in reality.
      "  restat = 1\n"
      "build header.h: true header.in\n"
      "build out: cat in1\n"
      "  deps = gcc\n"
      "  depfile = in1.d\n";
  {
    State state;
    ASSERT_NO_FATAL_FAILURE(AddCatRule(&state));
    ASSERT_NO_FATAL_FAILURE(AssertParse(&state, manifest));

    // Run the build once, everything should be ok.
    DepsLog deps_log;
    ASSERT_TRUE(deps_log.OpenForWrite("ninja_deps", &err));
    ASSERT_EQ("", err);

    Builder builder(&state, config_, NULL, &deps_log, &fs_);
    builder.command_runner_.reset(&command_runner_);
    EXPECT_TRUE(builder.AddTarget("out", &err));
    ASSERT_EQ("", err);
    fs_.Create("in1.d", "out: header.h");
    EXPECT_TRUE(builder.Build(&err));
    EXPECT_EQ("", err);

    deps_log.Close();
    builder.command_runner_.release();
  }

  {
    State state;
    ASSERT_NO_FATAL_FAILURE(AddCatRule(&state));
    ASSERT_NO_FATAL_FAILURE(AssertParse(&state, manifest));

    // Touch the input of the restat rule.
    fs_.Tick();
    fs_.Create("header.in", "");

    // Run the build again.
    DepsLog deps_log;
    ASSERT_TRUE(deps_log.Load("ninja_deps", &state, &err));
    ASSERT_TRUE(deps_log.OpenForWrite("ninja_deps", &err));

    Builder builder(&state, config_, NULL, &deps_log, &fs_);
    builder.command_runner_.reset(&command_runner_);
    command_runner_.commands_ran_.clear();
    EXPECT_TRUE(builder.AddTarget("out", &err));
    ASSERT_EQ("", err);
    EXPECT_TRUE(builder.Build(&err));
    EXPECT_EQ("", err);

    // Rule "true" should have run again, but the build of "out" should have
    // been cancelled due to restat propagating through the depfile header.
    EXPECT_EQ(1u, command_runner_.commands_ran_.size());

    builder.command_runner_.release();
  }
}

TEST_F(BuildWithDepsLogTest, DepFileOKDepsLog) {
  string err;
  const char* manifest =
      "rule cc\n  command = cc $in\n  depfile = $out.d\n  deps = gcc\n"
      "build fo$ o.o: cc foo.c\n";

  fs_.Create("foo.c", "");

  {
    State state;
    ASSERT_NO_FATAL_FAILURE(AssertParse(&state, manifest));

    // Run the build once, everything should be ok.
    DepsLog deps_log;
    ASSERT_TRUE(deps_log.OpenForWrite("ninja_deps", &err));
    ASSERT_EQ("", err);

    Builder builder(&state, config_, NULL, &deps_log, &fs_);
    builder.command_runner_.reset(&command_runner_);
    EXPECT_TRUE(builder.AddTarget("fo o.o", &err));
    ASSERT_EQ("", err);
    fs_.Create("fo o.o.d", "fo\\ o.o: blah.h bar.h\n");
    EXPECT_TRUE(builder.Build(&err));
    EXPECT_EQ("", err);

    deps_log.Close();
    builder.command_runner_.release();
  }

  {
    State state;
    ASSERT_NO_FATAL_FAILURE(AssertParse(&state, manifest));

    DepsLog deps_log;
    ASSERT_TRUE(deps_log.Load("ninja_deps", &state, &err));
    ASSERT_TRUE(deps_log.OpenForWrite("ninja_deps", &err));
    ASSERT_EQ("", err);

    Builder builder(&state, config_, NULL, &deps_log, &fs_);
    builder.command_runner_.reset(&command_runner_);

    Edge* edge = state.edges_.back();

    state.GetNode("bar.h", 0)->MarkDirty();  // Mark bar.h as missing.
    EXPECT_TRUE(builder.AddTarget("fo o.o", &err));
    ASSERT_EQ("", err);

    // Expect three new edges: one generating fo o.o, and two more from
    // loading the depfile.
    ASSERT_EQ(3u, state.edges_.size());
    // Expect our edge to now have three inputs: foo.c and two headers.
    ASSERT_EQ(3u, edge->inputs_.size());

    // Expect the command line we generate to only use the original input.
    ASSERT_EQ("cc foo.c", edge->EvaluateCommand());

    deps_log.Close();
    builder.command_runner_.release();
  }
}

#ifdef _WIN32
TEST_F(BuildWithDepsLogTest, DepFileDepsLogCanonicalize) {
  string err;
  const char* manifest =
      "rule cc\n  command = cc $in\n  depfile = $out.d\n  deps = gcc\n"
      "build a/b\\c\\d/e/fo$ o.o: cc x\\y/z\\foo.c\n";

  fs_.Create("x/y/z/foo.c", "");

  {
    State state;
    ASSERT_NO_FATAL_FAILURE(AssertParse(&state, manifest));

    // Run the build once, everything should be ok.
    DepsLog deps_log;
    ASSERT_TRUE(deps_log.OpenForWrite("ninja_deps", &err));
    ASSERT_EQ("", err);

    Builder builder(&state, config_, NULL, &deps_log, &fs_);
    builder.command_runner_.reset(&command_runner_);
    EXPECT_TRUE(builder.AddTarget("a/b/c/d/e/fo o.o", &err));
    ASSERT_EQ("", err);
    // Note, different slashes from manifest.
    fs_.Create("a/b\\c\\d/e/fo o.o.d",
               "a\\b\\c\\d\\e\\fo\\ o.o: blah.h bar.h\n");
    EXPECT_TRUE(builder.Build(&err));
    EXPECT_EQ("", err);

    deps_log.Close();
    builder.command_runner_.release();
  }

  {
    State state;
    ASSERT_NO_FATAL_FAILURE(AssertParse(&state, manifest));

    DepsLog deps_log;
    ASSERT_TRUE(deps_log.Load("ninja_deps", &state, &err));
    ASSERT_TRUE(deps_log.OpenForWrite("ninja_deps", &err));
    ASSERT_EQ("", err);

    Builder builder(&state, config_, NULL, &deps_log, &fs_);
    builder.command_runner_.reset(&command_runner_);

    Edge* edge = state.edges_.back();

    state.GetNode("bar.h", 0)->MarkDirty();  // Mark bar.h as missing.
    EXPECT_TRUE(builder.AddTarget("a/b/c/d/e/fo o.o", &err));
    ASSERT_EQ("", err);

    // Expect three new edges: one generating fo o.o, and two more from
    // loading the depfile.
    ASSERT_EQ(3u, state.edges_.size());
    // Expect our edge to now have three inputs: foo.c and two headers.
    ASSERT_EQ(3u, edge->inputs_.size());

    // Expect the command line we generate to only use the original input.
    // Note, slashes from manifest, not .d.
    ASSERT_EQ("cc x\\y/z\\foo.c", edge->EvaluateCommand());

    deps_log.Close();
    builder.command_runner_.release();
  }
}
#endif

/// Check that a restat rule doesn't clear an edge if the depfile is missing.
/// Follows from: https://github.com/ninja-build/ninja/issues/603
TEST_F(BuildTest, RestatMissingDepfile) {
const char* manifest =
"rule true\n"
"  command = true\n"  // Would be "write if out-of-date" in reality.
"  restat = 1\n"
"build header.h: true header.in\n"
"build out: cat header.h\n"
"  depfile = out.d\n";

  fs_.Create("header.h", "");
  fs_.Tick();
  fs_.Create("out", "");
  fs_.Create("header.in", "");

  // Normally, only 'header.h' would be rebuilt, as
  // its rule doesn't touch the output and has 'restat=1' set.
  // But we are also missing the depfile for 'out',
  // which should force its command to run anyway!
  RebuildTarget("out", manifest);
  ASSERT_EQ(2u, command_runner_.commands_ran_.size());
}

/// Check that a restat rule doesn't clear an edge if the deps are missing.
/// https://github.com/ninja-build/ninja/issues/603
TEST_F(BuildWithDepsLogTest, RestatMissingDepfileDepslog) {
  string err;
  const char* manifest =
"rule true\n"
"  command = true\n"  // Would be "write if out-of-date" in reality.
"  restat = 1\n"
"build header.h: true header.in\n"
"build out: cat header.h\n"
"  deps = gcc\n"
"  depfile = out.d\n";

  // Build once to populate ninja deps logs from out.d
  fs_.Create("header.in", "");
  fs_.Create("out.d", "out: header.h");
  fs_.Create("header.h", "");

  RebuildTarget("out", manifest, "build_log", "ninja_deps");
  ASSERT_EQ(2u, command_runner_.commands_ran_.size());

  // Sanity: this rebuild should be NOOP
  RebuildTarget("out", manifest, "build_log", "ninja_deps");
  ASSERT_EQ(0u, command_runner_.commands_ran_.size());

  // Touch 'header.in', blank dependencies log (create a different one).
  // Building header.h triggers 'restat' outputs cleanup.
  // Validate that out is rebuilt netherless, as deps are missing.
  fs_.Tick();
  fs_.Create("header.in", "");

  // (switch to a new blank deps_log "ninja_deps2")
  RebuildTarget("out", manifest, "build_log", "ninja_deps2");
  ASSERT_EQ(2u, command_runner_.commands_ran_.size());

  // Sanity: this build should be NOOP
  RebuildTarget("out", manifest, "build_log", "ninja_deps2");
  ASSERT_EQ(0u, command_runner_.commands_ran_.size());

  // Check that invalidating deps by target timestamp also works here
  // Repeat the test but touch target instead of blanking the log.
  fs_.Tick();
  fs_.Create("header.in", "");
  fs_.Create("out", "");
  RebuildTarget("out", manifest, "build_log", "ninja_deps2");
  ASSERT_EQ(2u, command_runner_.commands_ran_.size());

  // And this build should be NOOP again
  RebuildTarget("out", manifest, "build_log", "ninja_deps2");
  ASSERT_EQ(0u, command_runner_.commands_ran_.size());
}

TEST_F(BuildTest, WrongOutputInDepfileCausesRebuild) {
  string err;
  const char* manifest =
"rule cc\n"
"  command = cc $in\n"
"  depfile = $out.d\n"
"build foo.o: cc foo.c\n";

  fs_.Create("foo.c", "");
  fs_.Create("foo.o", "");
  fs_.Create("header.h", "");
  fs_.Create("foo.o.d", "bar.o.d: header.h\n");

  RebuildTarget("foo.o", manifest, "build_log", "ninja_deps");
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());
}

TEST_F(BuildTest, Console) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule console\n"
"  command = console\n"
"  pool = console\n"
"build cons: console in.txt\n"));

  fs_.Create("in.txt", "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("cons", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);
  ASSERT_EQ(1u, command_runner_.commands_ran_.size());
}
