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

#include "build_log.h"
#include "graph.h"
#include "test.h"

/// Fixture for tests involving Plan.
// Though Plan doesn't use State, it's useful to have one around
// to create Nodes and Edges.
struct PlanTest : public StateTestWithBuiltinRules {
  Plan plan_;
};

TEST_F(PlanTest, Basic) {
  AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n");
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

  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);
  ASSERT_EQ("mid", edge->inputs_[0]->path());
  ASSERT_EQ("out", edge->outputs_[0]->path());

  plan_.EdgeFinished(edge);

  ASSERT_FALSE(plan_.more_to_do());
  edge = plan_.FindWork();
  ASSERT_EQ(0, edge);
}

// Test that two outputs from one rule can be handled as inputs to the next.
TEST_F(PlanTest, DoubleOutputDirect) {
  AssertParse(&state_,
"build out: cat mid1 mid2\n"
"build mid1 mid2: cat in\n");
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
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat mid1 mid2
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_FALSE(edge);  // done
}

// Test that two outputs from one rule can eventually be routed to another.
TEST_F(PlanTest, DoubleOutputIndirect) {
  AssertParse(&state_,
"build out: cat b1 b2\n"
"build b1: cat a1\n"
"build b2: cat a2\n"
"build a1 a2: cat in\n");
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
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat a1
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat a2
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat b1 b2
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_FALSE(edge);  // done
}

// Test that two edges from one output can both execute.
TEST_F(PlanTest, DoubleDependent) {
  AssertParse(&state_,
"build out: cat a1 a2\n"
"build a1: cat mid\n"
"build a2: cat mid\n"
"build mid: cat in\n");
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
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat mid
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat mid
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat a1 a2
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_FALSE(edge);  // done
}

TEST_F(PlanTest, DependencyCycle) {
  AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n"
"build in: cat pre\n"
"build pre: cat out\n");
  GetNode("out")->MarkDirty();
  GetNode("mid")->MarkDirty();
  GetNode("in")->MarkDirty();
  GetNode("pre")->MarkDirty();

  string err;
  EXPECT_FALSE(plan_.AddTarget(GetNode("out"), &err));
  ASSERT_EQ("dependency cycle: out -> mid -> in -> pre -> out", err);
}

struct BuildTest : public StateTestWithBuiltinRules,
                   public CommandRunner {
  BuildTest() : config_(MakeConfig()), builder_(&state_, config_), now_(1),
                last_command_(NULL), status_(config_) {
    builder_.disk_interface_ = &fs_;
    builder_.command_runner_.reset(this);
    AssertParse(&state_,
"build cat1: cat in1\n"
"build cat2: cat in1 in2\n"
"build cat12: cat cat1 cat2\n");

    fs_.Create("in1", now_, "");
    fs_.Create("in2", now_, "");
  }

  ~BuildTest() {
    builder_.command_runner_.release();
  }

  // Mark a path dirty.
  void Dirty(const string& path);

  // CommandRunner impl
  virtual bool CanRunMore();
  virtual bool StartCommand(Edge* edge);
  virtual Edge* WaitForCommand(ExitStatus* status, string* output);
  virtual vector<Edge*> GetActiveEdges();
  virtual void Abort();

  BuildConfig MakeConfig() {
    BuildConfig config;
    config.verbosity = BuildConfig::QUIET;
    return config;
  }

  BuildConfig config_;
  Builder builder_;
  int now_;

  VirtualFileSystem fs_;

  vector<string> commands_ran_;
  Edge* last_command_;
  BuildStatus status_;
};

void BuildTest::Dirty(const string& path) {
  Node* node = GetNode(path);
  node->MarkDirty();

  // If it's an input file, mark that we've already stat()ed it and
  // it's missing.
  if (!node->in_edge())
    node->MarkMissing();
}

bool BuildTest::CanRunMore() {
  // Only run one at a time.
  return last_command_ == NULL;
}

bool BuildTest::StartCommand(Edge* edge) {
  assert(!last_command_);
  commands_ran_.push_back(edge->EvaluateCommand());
  if (edge->rule().name() == "cat"  ||
      edge->rule().name() == "cat_rsp" ||
      edge->rule().name() == "cc" ||
      edge->rule().name() == "touch" ||
      edge->rule().name() == "touch-interrupt") {
    for (vector<Node*>::iterator out = edge->outputs_.begin();
         out != edge->outputs_.end(); ++out) {
      fs_.Create((*out)->path(), now_, "");
    }
  } else if (edge->rule().name() == "true" ||
             edge->rule().name() == "fail" ||
             edge->rule().name() == "interrupt") {
    // Don't do anything.
  } else {
    printf("unknown command\n");
    return false;
  }

  last_command_ = edge;
  return true;
}

Edge* BuildTest::WaitForCommand(ExitStatus* status, string* /* output */) {
  if (Edge* edge = last_command_) {
    if (edge->rule().name() == "interrupt" ||
        edge->rule().name() == "touch-interrupt") {
      *status = ExitInterrupted;
      return NULL;
    }

    if (edge->rule().name() == "fail")
      *status = ExitFailure;
    else
      *status = ExitSuccess;
    last_command_ = NULL;
    return edge;
  }
  *status = ExitFailure;
  return NULL;
}

vector<Edge*> BuildTest::GetActiveEdges() {
  vector<Edge*> edges;
  if (last_command_)
    edges.push_back(last_command_);
  return edges;
}

void BuildTest::Abort() {
  last_command_ = NULL;
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

  ASSERT_EQ(1u, commands_ran_.size());
  EXPECT_EQ("cat in1 > cat1", commands_ran_[0]);
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

  ASSERT_EQ(1u, commands_ran_.size());
  EXPECT_EQ("cat in1 > cat1", commands_ran_[0]);
}

TEST_F(BuildTest, TwoStep) {
  string err;
  EXPECT_TRUE(builder_.AddTarget("cat12", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);
  ASSERT_EQ(3u, commands_ran_.size());
  // Depending on how the pointers work out, we could've ran
  // the first two commands in either order.
  EXPECT_TRUE((commands_ran_[0] == "cat in1 > cat1" &&
               commands_ran_[1] == "cat in1 in2 > cat2") ||
              (commands_ran_[1] == "cat in1 > cat1" &&
               commands_ran_[0] == "cat in1 in2 > cat2"));

  EXPECT_EQ("cat cat1 cat2 > cat12", commands_ran_[2]);

  now_++;

  // Modifying in2 requires rebuilding one intermediate file
  // and the final file.
  fs_.Create("in2", now_, "");
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("cat12", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(5u, commands_ran_.size());
  EXPECT_EQ("cat in1 in2 > cat2", commands_ran_[3]);
  EXPECT_EQ("cat cat1 cat2 > cat12", commands_ran_[4]);
}

TEST_F(BuildTest, TwoOutputs) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule touch\n"
"  command = touch $out\n"
"build out1 out2: touch in.txt\n"));

  fs_.Create("in.txt", now_, "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("out1", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);
  ASSERT_EQ(1u, commands_ran_.size());
  EXPECT_EQ("touch out1 out2", commands_ran_[0]);
}

// Test case from
//   https://github.com/martine/ninja/issues/148
TEST_F(BuildTest, MultiOutIn) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule touch\n"
"  command = touch $out\n"
"build in1 otherfile: touch in\n"
"build out: touch in | in1\n"));

  fs_.Create("in", now_, "");
  fs_.Create("in1", ++now_, "");

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

  fs_.Create("c1", now_, "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("c5", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);
  ASSERT_EQ(4u, commands_ran_.size());

  err.clear();
  commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("c5", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.AlreadyUpToDate());

  now_++;

  fs_.Create("c3", now_, "");
  err.clear();
  commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("c5", &err));
  ASSERT_EQ("", err);
  EXPECT_FALSE(builder_.AlreadyUpToDate());
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(2u, commands_ran_.size());  // 3->4, 4->5
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
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_, "build subdir\\dir2\\file: cat in1\n"));
  EXPECT_TRUE(builder_.AddTarget("subdir\\dir2\\file", &err));
#else
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_, "build subdir/dir2/file: cat in1\n"));
  EXPECT_TRUE(builder_.AddTarget("subdir/dir2/file", &err));
#endif

  EXPECT_EQ("", err);
  now_ = 0;  // Make all stat()s return file not found.
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(2u, fs_.directories_made_.size());
  EXPECT_EQ("subdir", fs_.directories_made_[0]);
#ifdef _WIN32
  EXPECT_EQ("subdir\\dir2", fs_.directories_made_[1]);
#else
  EXPECT_EQ("subdir/dir2", fs_.directories_made_[1]);
#endif
}

TEST_F(BuildTest, DepFileMissing) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n  command = cc $in\n  depfile = $out.d\n"
"build foo.o: cc foo.c\n"));
  fs_.Create("foo.c", now_, "");

  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, fs_.files_read_.size());
  EXPECT_EQ("foo.o.d", fs_.files_read_[0]);
}

TEST_F(BuildTest, DepFileOK) {
  string err;
  int orig_edges = state_.edges_.size();
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n  command = cc $in\n  depfile = $out.d\n"
"build foo.o: cc foo.c\n"));
  Edge* edge = state_.edges_.back();

  fs_.Create("foo.c", now_, "");
  GetNode("bar.h")->MarkDirty();  // Mark bar.h as missing.
  fs_.Create("foo.o.d", now_, "foo.o: blah.h bar.h\n");
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
  fs_.Create("foo.c", now_, "");
  fs_.Create("foo.o.d", now_, "randomtext\n");
  EXPECT_FALSE(builder_.AddTarget("foo.o", &err));
  EXPECT_EQ("expected depfile 'foo.o.d' to mention 'foo.o', got 'randomtext'",
            err);
}

TEST_F(BuildTest, OrderOnlyDeps) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n  command = cc $in\n  depfile = $out.d\n"
"build foo.o: cc foo.c || otherfile\n"));
  Edge* edge = state_.edges_.back();

  fs_.Create("foo.c", now_, "");
  fs_.Create("otherfile", now_, "");
  fs_.Create("foo.o.d", now_, "foo.o: blah.h bar.h\n");
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
  ASSERT_EQ(1u, commands_ran_.size());

  now_++;

  // implicit dep dirty, expect a rebuild.
  fs_.Create("blah.h", now_, "");
  fs_.Create("bar.h", now_, "");
  commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, commands_ran_.size());

  now_++;

  // order only dep dirty, no rebuild.
  fs_.Create("otherfile", now_, "");
  commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_EQ("", err);
  EXPECT_TRUE(builder_.AlreadyUpToDate());

  // implicit dep missing, expect rebuild.
  fs_.RemoveFile("bar.h");
  commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, commands_ran_.size());
}

TEST_F(BuildTest, RebuildOrderOnlyDeps) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n  command = cc $in\n"
"rule true\n  command = true\n"
"build oo.h: cc oo.h.in\n"
"build foo.o: cc foo.c || oo.h\n"));

  fs_.Create("foo.c", now_, "");
  fs_.Create("oo.h.in", now_, "");

  // foo.o and order-only dep dirty, build both.
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(2u, commands_ran_.size());

  // all clean, no rebuild.
  commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_EQ("", err);
  EXPECT_TRUE(builder_.AlreadyUpToDate());

  // order-only dep missing, build it only.
  fs_.RemoveFile("oo.h");
  commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, commands_ran_.size());
  ASSERT_EQ("cc oo.h.in", commands_ran_[0]);

  now_++;

  // order-only dep dirty, build it only.
  fs_.Create("oo.h.in", now_, "");
  commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, commands_ran_.size());
  ASSERT_EQ("cc oo.h.in", commands_ran_[0]);
}

TEST_F(BuildTest, Phony) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat bar.cc\n"
"build all: phony out\n"));
  fs_.Create("bar.cc", now_, "");

  EXPECT_TRUE(builder_.AddTarget("all", &err));
  ASSERT_EQ("", err);

  // Only one command to run, because phony runs no command.
  EXPECT_FALSE(builder_.AlreadyUpToDate());
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, commands_ran_.size());
}

TEST_F(BuildTest, PhonyNoWork) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat bar.cc\n"
"build all: phony out\n"));
  fs_.Create("bar.cc", now_, "");
  fs_.Create("out", now_, "");

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
  ASSERT_EQ(1u, commands_ran_.size());
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
  ASSERT_EQ(3u, commands_ran_.size());
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
  ASSERT_EQ(3u, commands_ran_.size());
  ASSERT_EQ("cannot make progress due to previous errors", err);
}

struct BuildWithLogTest : public BuildTest {
  BuildWithLogTest() {
    state_.build_log_ = builder_.log_ = &build_log_;
  }

  BuildLog build_log_;
};

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

  fs_.Create("out1", now_, "");
  fs_.Create("out2", now_, "");
  fs_.Create("out3", now_, "");

  now_++;

  fs_.Create("in", now_, "");

  // "cc" touches out1, so we should build out2.  But because "true" does not
  // touch out2, we should cancel the build of out3.
  string err;
  EXPECT_TRUE(builder_.AddTarget("out3", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(2u, commands_ran_.size());

  // If we run again, it should be a no-op, because the build log has recorded
  // that we've already built out2 with an input timestamp of 2 (from out1).
  commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("out3", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.AlreadyUpToDate());

  now_++;

  fs_.Create("in", now_, "");

  // The build log entry should not, however, prevent us from rebuilding out2
  // if out1 changes.
  commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("out3", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(2u, commands_ran_.size());
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

  fs_.Create("in", now_, "");
  fs_.Create("out2", now_, "");

  // Run a build, expect only the first command to run.
  // It doesn't touch its output (due to being the "true" command), so
  // we shouldn't run the dependent build.
  string err;
  EXPECT_TRUE(builder_.AddTarget("out2", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(1u, commands_ran_.size());
}

// Test scenario, in which an input file is removed, but output isn't changed
// https://github.com/martine/ninja/issues/295
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
  fs_.Create("in", now_, "");

  // The implicit dependencies and the depfile itself 
  // are newer than the output
  TimeStamp restat_mtime = ++now_;
  fs_.Create("out1.d", now_, "out1: will.be.deleted restat.file\n");
  fs_.Create("will.be.deleted", now_, "");
  fs_.Create("restat.file", now_, "");

  // Run the build, out1 and out2 get built
  string err;
  EXPECT_TRUE(builder_.AddTarget("out2", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(2u, commands_ran_.size());

  // See that an entry in the logfile is created, capturing
  // the right mtime
  BuildLog::LogEntry * log_entry = build_log_.LookupByOutput("out1");
  ASSERT_TRUE(NULL != log_entry);
  ASSERT_EQ(restat_mtime, log_entry->restat_mtime);

  // Now remove a file, referenced from depfile, so that target becomes 
  // dirty, but the output does not change
  fs_.RemoveFile("will.be.deleted");
  
  // Trigger the build again - only out1 gets built
  commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("out2", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(1u, commands_ran_.size());

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

  fs_.Create("out1", now_, "");
  fs_.Create("out2", now_, "");
  fs_.Create("out3", now_, "");

  now_++;

  fs_.Create("in", now_, "");

  // "cc" touches out1, so we should build out2.  But because "true" does not
  // touch out2, we should cancel the build of out3.
  string err;
  EXPECT_TRUE(builder_.AddTarget("out3", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(3u, commands_ran_.size());
}

// Test that RSP files are created when & where appropriate and deleted after
// succesful execution.
TEST_F(BuildTest, RspFileSuccess)
{
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "rule cat_rsp\n"
    "  command = cat $rspfile > $out\n"
    "  rspfile = $rspfile\n"
    "  rspfile_content = $long_command\n"
    "build out1: cat in\n"
    "build out2: cat_rsp in\n"
    "  rspfile = out2.rsp\n"
    "  long_command = Some very long command\n"));

  fs_.Create("out1", now_, "");
  fs_.Create("out2", now_, "");
  fs_.Create("out3", now_, "");

  now_++;

  fs_.Create("in", now_, "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("out1", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.AddTarget("out2", &err));
  ASSERT_EQ("", err);

  size_t files_created = fs_.files_created_.size();
  size_t files_removed = fs_.files_removed_.size();

  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(2u, commands_ran_.size()); // cat + cat_rsp

  // The RSP file was created
  ASSERT_EQ(files_created + 1, fs_.files_created_.size());
  ASSERT_EQ(1u, fs_.files_created_.count("out2.rsp"));

  // The RSP file was removed
  ASSERT_EQ(files_removed + 1, fs_.files_removed_.size());
  ASSERT_EQ(1u, fs_.files_removed_.count("out2.rsp"));
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

  fs_.Create("out", now_, "");
  now_++;
  fs_.Create("in", now_, "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("out", &err));
  ASSERT_EQ("", err);

  size_t files_created = fs_.files_created_.size();
  size_t files_removed = fs_.files_removed_.size();

  EXPECT_FALSE(builder_.Build(&err));
  ASSERT_EQ("subcommand failed", err);
  ASSERT_EQ(1u, commands_ran_.size());

  // The RSP file was created
  ASSERT_EQ(files_created + 1, fs_.files_created_.size());
  ASSERT_EQ(1u, fs_.files_created_.count("out.rsp"));

  // The RSP file was NOT removed
  ASSERT_EQ(files_removed, fs_.files_removed_.size());
  ASSERT_EQ(0u, fs_.files_removed_.count("out.rsp"));

  // The RSP file contains what it should
  ASSERT_EQ("Another very long command", fs_.files_["out.rsp"].contents);
}

// Test that contens of the RSP file behaves like a regular part of
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

  fs_.Create("out", now_, "");
  now_++;
  fs_.Create("in", now_, "");

  string err;
  EXPECT_TRUE(builder_.AddTarget("out", &err));
  ASSERT_EQ("", err);

  // 1. Build for the 1st time (-> populate log)
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(1u, commands_ran_.size());

  // 2. Build again (no change)
  commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("out", &err));
  EXPECT_EQ("", err);
  ASSERT_TRUE(builder_.AlreadyUpToDate());

  // 3. Alter the entry in the logfile
  // (to simulate a change in the command line between 2 builds)
  BuildLog::LogEntry * log_entry = build_log_.LookupByOutput("out");
  ASSERT_TRUE(NULL != log_entry);
  ASSERT_NO_FATAL_FAILURE(AssertHash(
        "cat out.rsp > out;rspfile=Original very long command",
        log_entry->command_hash));
  log_entry->command_hash++;  // Change the command hash to something else.
  // Now expect the target to be rebuilt
  commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("out", &err));
  EXPECT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ(1u, commands_ran_.size());
}

TEST_F(BuildTest, InterruptCleanup) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule interrupt\n"
"  command = interrupt\n"
"rule touch-interrupt\n"
"  command = touch-interrupt\n"
"build out1: interrupt in1\n"
"build out2: touch-interrupt in2\n"));

  fs_.Create("out1", now_, "");
  fs_.Create("out2", now_, "");
  now_++;
  fs_.Create("in1", now_, "");
  fs_.Create("in2", now_, "");

  // An untouched output of an interrupted command should be retained.
  string err;
  EXPECT_TRUE(builder_.AddTarget("out1", &err));
  EXPECT_EQ("", err);
  EXPECT_FALSE(builder_.Build(&err));
  EXPECT_EQ("interrupted by user", err);
  builder_.Cleanup();
  EXPECT_EQ(now_-1, fs_.Stat("out1"));
  err = "";

  // A touched output of an interrupted command should be deleted.
  EXPECT_TRUE(builder_.AddTarget("out2", &err));
  EXPECT_EQ("", err);
  EXPECT_FALSE(builder_.Build(&err));
  EXPECT_EQ("interrupted by user", err);
  builder_.Cleanup();
  EXPECT_EQ(0, fs_.Stat("out2"));
}

TEST_F(BuildTest, PhonyWithNoInputs) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build nonexistent: phony\n"
"build out1: cat || nonexistent\n"
"build out2: cat nonexistent\n"));
  fs_.Create("out1", now_, "");
  fs_.Create("out2", now_, "");

  // out1 should be up to date even though its input is dirty, because its
  // order-only dependency has nothing to do.
  string err;
  EXPECT_TRUE(builder_.AddTarget("out1", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.AlreadyUpToDate());

  // out2 should still be out of date though, because its input is dirty.
  err.clear();
  commands_ran_.clear();
  state_.Reset();
  EXPECT_TRUE(builder_.AddTarget("out2", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);
  ASSERT_EQ(1u, commands_ran_.size());
}

TEST_F(BuildTest, StatusFormatReplacePlaceholder) {
  EXPECT_EQ("[%/s0/t0/r0/u0/f0]",
            status_.FormatProgressStatus("[%%/s%s/t%t/r%r/u%u/f%f]"));
}
