#include "build.h"

#include "test.h"

// Though Plan doesn't use State, it's useful to have one around
// to create Nodes and Edges.
struct PlanTest : public StateTestWithBuiltinRules {
  Plan plan_;
};

TEST_F(PlanTest, Basic) {
  AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n");
  GetNode("in")->MarkDependentsDirty();
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
  ASSERT_EQ(NULL, edge);
}

// Test that two outputs from one rule can be handled as inputs to the next.
TEST_F(PlanTest, DoubleOutputDirect) {
  AssertParse(&state_,
"build out: cat mid1 mid2\n"
"build mid1 mid2: cat in\n");
  GetNode("in")->MarkDependentsDirty();
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
  GetNode("in")->MarkDependentsDirty();
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
  GetNode("in")->MarkDependentsDirty();
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
  GetNode("pre")->MarkDependentsDirty();
  string err;
  EXPECT_FALSE(plan_.AddTarget(GetNode("out"), &err));
  ASSERT_EQ("dependency cycle: out -> mid -> in -> pre -> out", err);
}

struct BuildTest : public StateTestWithBuiltinRules,
                   public CommandRunner,
                   public DiskInterface {
  BuildTest() : config_(MakeConfig()), builder_(&state_, config_), now_(1),
                last_command_(NULL) {
    builder_.disk_interface_ = this;
    builder_.command_runner_ = this;
    AssertParse(&state_,
"build cat1: cat in1\n"
"build cat2: cat in1 in2\n"
"build cat12: cat cat1 cat2\n");
  }

  // Mark a path dirty.
  void Dirty(const string& path);
  // Mark dependents of a path dirty.
  void Touch(const string& path);

  // CommandRunner impl
  virtual bool CanRunMore();
  virtual bool StartCommand(Edge* edge);
  virtual bool WaitForCommands();
  virtual Edge* NextFinishedCommand(bool* success);

  // DiskInterface
  virtual int Stat(const string& path) {
    return now_;
  }
  virtual bool MakeDir(const string& path) {
    directories_made_.push_back(path);
    return true;  // success
  }
  virtual string ReadFile(const string& path, string* err) {
    files_read_.push_back(path);
    map<string, string>::iterator i = file_contents_.find(path);
    if (i != file_contents_.end())
      return i->second;
    return "";
  }

  BuildConfig MakeConfig() {
    BuildConfig config;
    config.verbosity = BuildConfig::QUIET;
    return config;
  }

  BuildConfig config_;
  Builder builder_;
  int now_;
  map<string, string> file_contents_;
  vector<string> commands_ran_;
  vector<string> directories_made_;
  vector<string> files_read_;
  Edge* last_command_;
};

void BuildTest::Dirty(const string& path) {
  Node* node = GetNode(path);
  node->MarkDirty();

  // If it's an input file, mark that we've already stat()ed it and
  // it's missing.
  if (!node->in_edge_)
    node->file_->mtime_ = 0;
}

void BuildTest::Touch(const string& path) {
  Node* node = GetNode(path);
  assert(node);
  node->MarkDependentsDirty();
}

bool BuildTest::CanRunMore() {
  // Only run one at a time.
  return last_command_ == NULL;
}

bool BuildTest::StartCommand(Edge* edge) {
  assert(!last_command_);
  commands_ran_.push_back(edge->EvaluateCommand());
  if (edge->rule_->name_ == "cat" || edge->rule_->name_ == "cc") {
    for (vector<Node*>::iterator out = edge->outputs_.begin();
         out != edge->outputs_.end(); ++out) {
      (*out)->file_->Touch(now_);
    }
    last_command_ = edge;
    return true;
  } else {
    printf("unkown command\n");
  }

  return false;
}

bool BuildTest::WaitForCommands() {
  return true;
}

Edge* BuildTest::NextFinishedCommand(bool* success) {
  if (Edge* edge = last_command_) {
    *success = true;
    last_command_ = NULL;
    return edge;
  }
  return NULL;
}

TEST_F(BuildTest, NoWork) {
  string err;
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("no work to do", err);
  EXPECT_EQ(0, commands_ran_.size());
}

TEST_F(BuildTest, OneStep) {
  // Given a dirty target with one ready input,
  // we should rebuild the target.
  Dirty("cat1");
  string err;
  ASSERT_TRUE(builder_.AddTarget("cat1", &err));
  EXPECT_TRUE(builder_.Build(&err));
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
  EXPECT_TRUE(builder_.Build(&err));
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
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);
  ASSERT_EQ(3, commands_ran_.size());
  EXPECT_EQ("cat in1 > cat1", commands_ran_[0]);
  EXPECT_EQ("cat in1 in2 > cat2", commands_ran_[1]);
  EXPECT_EQ("cat cat1 cat2 > cat12", commands_ran_[2]);

  // Modifying in2 requires rebuilding one intermediate file
  // and the final file.
  Touch("in2");
  ASSERT_TRUE(builder_.AddTarget("cat12", &err));
  EXPECT_TRUE(builder_.Build(&err));
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
  EXPECT_TRUE(builder_.Build(&err));
  EXPECT_EQ("", err);
  ASSERT_EQ(4, commands_ran_.size());

  err.clear();
  commands_ran_.clear();
  EXPECT_FALSE(builder_.AddTarget("c5", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(0, commands_ran_.size());

  Touch("c3");  // Will recursively dirty through to c5.
  err.clear();
  commands_ran_.clear();
  EXPECT_TRUE(builder_.AddTarget("c5", &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ(2, commands_ran_.size());  // 3->4, 4->5
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
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build subdir/dir2/file: cat in1\n"));

  Touch("in1");
  EXPECT_TRUE(builder_.AddTarget("subdir/dir2/file", &err));
  EXPECT_EQ("", err);
  now_ = 0;  // Make all stat()s return file not found.
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(2, directories_made_.size());
  EXPECT_EQ("subdir", directories_made_[0]);
  EXPECT_EQ("subdir/dir2", directories_made_[1]);
}

TEST_F(BuildTest, DepFileMissing) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n  command = cc $in\n  depfile = $out.d\n"
"build foo.o: cc foo.c\n"));
  Touch("foo.c");
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1, files_read_.size());
  EXPECT_EQ("foo.o.d", files_read_[0]);
}

TEST_F(BuildTest, DepFileOK) {
  string err;
  int orig_edges = state_.edges_.size();
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n  command = cc $in\n  depfile = $out.d\n"
"build foo.o: cc foo.c\n"));
  Touch("foo.c");
  Dirty("bar.h");  // Mark bar.h as missing.
  file_contents_["foo.o.d"] = "foo.o: blah.h bar.h\n";
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1, files_read_.size());
  EXPECT_EQ("foo.o.d", files_read_[0]);

  // Expect our edge to now have three inputs: foo.c and two headers.
  ASSERT_EQ(orig_edges + 1, state_.edges_.size());
  Edge* edge = state_.edges_.back();
  ASSERT_EQ(3, edge->inputs_.size());

  // Expect the command line we generate to only use the original input.
  ASSERT_EQ("cc foo.c", edge->EvaluateCommand());
}

TEST_F(BuildTest, DepFileParseError) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n  command = cc $in\n  depfile = $out.d\n"
"build foo.o: cc foo.c\n"));
  Touch("foo.c");
  file_contents_["foo.o.d"] = "foo.o blah.h bar.h\n";
  EXPECT_FALSE(builder_.AddTarget("foo.o", &err));
  EXPECT_EQ("line 1, col 7: expected ':', got 'blah.h'", err);
}

TEST_F(BuildTest, OrderOnlyDeps) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cc\n  command = cc $in\n  depfile = $out.d\n"
"build foo.o: cc foo.c | otherfile\n"));
  Touch("foo.c");
  file_contents_["foo.o.d"] = "foo.o: blah.h bar.h\n";
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));

  Edge* edge = state_.edges_.back();
  // One explicit, two implicit, one order only.
  ASSERT_EQ(4, edge->inputs_.size());
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
  ASSERT_EQ(1, commands_ran_.size());

  // implicit dep dirty, expect a rebuild.
  commands_ran_.clear();
  Touch("blah.h");
  EXPECT_TRUE(builder_.AddTarget("foo.o", &err));
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1, commands_ran_.size());

  // order only dep dirty, no rebuild.
  commands_ran_.clear();
  Touch("otherfile");
  // We should fail to even add the depenency on foo.o, because
  // there's nothing to do.
  EXPECT_FALSE(builder_.AddTarget("foo.o", &err));
}

TEST_F(BuildTest, Phony) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat bar.cc\n"
"build all: phony out\n"));
  Touch("bar.cc");

  EXPECT_TRUE(builder_.AddTarget("all", &err));

  // Only one command to run, because phony runs no command.
  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1, commands_ran_.size());

  EXPECT_TRUE(builder_.Build(&err));
  ASSERT_NE("", err);
}

