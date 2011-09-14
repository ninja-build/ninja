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
  GetNode("mid")->dirty_ = true;
  GetNode("out")->dirty_ = true;
  string err;
  EXPECT_TRUE(plan_.AddTarget(GetNode("out"), &err));
  ASSERT_EQ("", err);
  ASSERT_TRUE(plan_.more_to_do());

  Edge* edge = plan_.FindWork();
  ASSERT_TRUE(edge);
  ASSERT_EQ("in",  edge->inputs_[0]->file_->path_);
  ASSERT_EQ("mid", edge->outputs_[0]->file_->path_);

  ASSERT_FALSE(plan_.FindWork());

  GetNode("mid")->dirty_ = false;
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);
  ASSERT_EQ("mid", edge->inputs_[0]->file_->path_);
  ASSERT_EQ("out", edge->outputs_[0]->file_->path_);

  GetNode("out")->dirty_ = false;
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
  GetNode("mid1")->dirty_ = true;
  GetNode("mid2")->dirty_ = true;
  GetNode("out")->dirty_ = true;

  string err;
  EXPECT_TRUE(plan_.AddTarget(GetNode("out"), &err));
  ASSERT_EQ("", err);
  ASSERT_TRUE(plan_.more_to_do());

  Edge* edge;
  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat in
  GetNode("mid1")->dirty_ = false;
  GetNode("mid2")->dirty_ = false;
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat mid1 mid2
  GetNode("in")->dirty_ = false;
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
  GetNode("a1")->dirty_ = true;
  GetNode("a2")->dirty_ = true;
  GetNode("b1")->dirty_ = true;
  GetNode("b2")->dirty_ = true;
  GetNode("out")->dirty_ = true;
  string err;
  EXPECT_TRUE(plan_.AddTarget(GetNode("out"), &err));
  ASSERT_EQ("", err);
  ASSERT_TRUE(plan_.more_to_do());

  Edge* edge;
  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat in
  GetNode("a1")->dirty_ = false;
  GetNode("a2")->dirty_ = false;
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat a1
  GetNode("b1")->dirty_ = false;
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat a2
  GetNode("b2")->dirty_ = false;
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat b1 b2
  GetNode("out")->dirty_ = false;
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
  GetNode("mid")->dirty_ = true;
  GetNode("a1")->dirty_ = true;
  GetNode("a2")->dirty_ = true;
  GetNode("out")->dirty_ = true;

  string err;
  EXPECT_TRUE(plan_.AddTarget(GetNode("out"), &err));
  ASSERT_EQ("", err);
  ASSERT_TRUE(plan_.more_to_do());

  Edge* edge;
  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat in
  GetNode("mid")->dirty_ = false;
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat mid
  GetNode("a1")->dirty_ = false;
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat mid
  GetNode("a2")->dirty_ = false;
  plan_.EdgeFinished(edge);

  edge = plan_.FindWork();
  ASSERT_TRUE(edge);  // cat a1 a2
  GetNode("out")->dirty_ = false;
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
  GetNode("out")->dirty_ = true;
  GetNode("mid")->dirty_ = true;
  GetNode("in")->dirty_ = true;
  GetNode("pre")->dirty_ = true;

  string err;
  EXPECT_FALSE(plan_.AddTarget(GetNode("out"), &err));
  ASSERT_EQ("dependency cycle: out -> mid -> in -> pre -> out", err);
}

struct BuildTest : public StateTestWithBuiltinRules,
                   public CommandRunner {
  BuildTest() : config_(MakeConfig()), builder_(&state_, config_), now_(1),
                last_command_(NULL) {
    builder_.disk_interface_ = &fs_;
    builder_.command_runner_ = this;
    AssertParse(&state_,
"build cat1: cat in1\n"
"build cat2: cat in1 in2\n"
"build cat12: cat cat1 cat2\n");

    fs_.Create("in1", now_, "");
    fs_.Create("in2", now_, "");
  }

  // Mark a path dirty.
  void Dirty(const string& path);

  // CommandRunner impl
  virtual bool CanRunMore();
  virtual bool StartCommand(Edge* edge);
  virtual Edge* WaitForCommand(bool* success, string* output);

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
};

void BuildTest::Dirty(const string& path) {
  Node* node = GetNode(path);
  node->dirty_ = true;

  // If it's an input file, mark that we've already stat()ed it and
  // it's missing.
  if (!node->in_edge_)
    node->file_->mtime_ = 0;
}

bool BuildTest::CanRunMore() {
  // Only run one at a time.
  return last_command_ == NULL;
}

bool BuildTest::StartCommand(Edge* edge) {
  assert(!last_command_);
  commands_ran_.push_back(edge->EvaluateCommand());
  if (edge->rule_->name_ == "cat" || edge->rule_->name_ == "cc" ||
        edge->rule_->name_ == "touch") {
    for (vector<Node*>::iterator out = edge->outputs_.begin();
         out != edge->outputs_.end(); ++out) {
      fs_.Create((*out)->file_->path_, now_, "");
    }
  } else if (edge->rule_->name_ == "fail") {
    // Don't do anything.
  } else {
    printf("unknown command\n");
    return false;
  }

  last_command_ = edge;
  return true;
}

Edge* BuildTest::WaitForCommand(bool* success, string* output) {
  if (Edge* edge = last_command_) {
    if (edge->rule_->name_ == "fail")
      *success = false;
    else
      *success = true;
    last_command_ = NULL;
    return edge;
  }
  return NULL;
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

  // Modifying in2 requires rebuilding one intermediate file
  // and the final file.
  GetNode("cat2")->dirty_ = true;
  GetNode("cat12")->dirty_ = true;
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
  state_.stat_cache_.Invalidate();
  EXPECT_TRUE(builder_.AddTarget("c5", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.AlreadyUpToDate());

  now_++;

  fs_.Create("c3", now_, "");
  err.clear();
  commands_ran_.clear();
  state_.stat_cache_.Invalidate();
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

#ifdef WIN32
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
#ifdef WIN32
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
  GetNode("bar.h")->dirty_ = true;  // Mark bar.h as missing.
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
  fs_.Create("foo.o.d", now_, "foo.o blah.h bar.h\n");
  EXPECT_FALSE(builder_.AddTarget("foo.o", &err));
  EXPECT_EQ("foo.o.d: line 1, col 7: expected ':', got 'blah.h'", err);
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
  EXPECT_EQ("foo.c", edge->inputs_[0]->file_->path_);
  EXPECT_EQ("blah.h", edge->inputs_[1]->file_->path_);
  EXPECT_EQ("bar.h", edge->inputs_[2]->file_->path_);
  EXPECT_EQ("otherfile", edge->inputs_[3]->file_->path_);

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
  state_.stat_cache_.Invalidate();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, commands_ran_.size());

  now_++;

  // order only dep dirty, no rebuild.
  fs_.Create("otherfile", now_, "");
  commands_ran_.clear();
  state_.stat_cache_.Invalidate();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_EQ("", err);
  EXPECT_TRUE(builder_.AlreadyUpToDate());

  // implicit dep missing, expect rebuild.
  fs_.RemoveFile("bar.h");
  commands_ran_.clear();
  state_.stat_cache_.Invalidate();
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, commands_ran_.size());
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
  config_.swallow_failures = 2;

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
  config_.swallow_failures = 10;

  string err;
  EXPECT_TRUE(builder_.AddTarget("final", &err));
  ASSERT_EQ("", err);

  EXPECT_FALSE(builder_.Build(&err));
  ASSERT_EQ(3u, commands_ran_.size());
  ASSERT_EQ("cannot make progress due to previous errors", err);
}
