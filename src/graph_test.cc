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

#include "graph.h"
#include "build.h"

#include "test.h"

struct GraphTest : public StateTestWithBuiltinRules {
  GraphTest() : scan_(&state_, NULL, NULL, &fs_) {}

  VirtualFileSystem fs_;
  DependencyScan scan_;
};

TEST_F(GraphTest, MissingImplicit) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat in | implicit\n"));
  fs_.Create("in", "");
  fs_.Create("out", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out"), &err));
  ASSERT_EQ("", err);

  // A missing implicit dep *should* make the output dirty.
  // (In fact, a build will fail.)
  // This is a change from prior semantics of ninja.
  EXPECT_TRUE(GetNode("out")->dirty());
}

TEST_F(GraphTest, ModifiedImplicit) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat in | implicit\n"));
  fs_.Create("in", "");
  fs_.Create("out", "");
  fs_.Tick();
  fs_.Create("implicit", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out"), &err));
  ASSERT_EQ("", err);

  // A modified implicit dep should make the output dirty.
  EXPECT_TRUE(GetNode("out")->dirty());
}

TEST_F(GraphTest, FunkyMakefilePath) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule catdep\n"
"  depfile = $out.d\n"
"  command = cat $in > $out\n"
"build out.o: catdep foo.cc\n"));
  fs_.Create("foo.cc",  "");
  fs_.Create("out.o.d", "out.o: ./foo/../implicit.h\n");
  fs_.Create("out.o", "");
  fs_.Tick();
  fs_.Create("implicit.h", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out.o"), &err));
  ASSERT_EQ("", err);

  // implicit.h has changed, though our depfile refers to it with a
  // non-canonical path; we should still find it.
  EXPECT_TRUE(GetNode("out.o")->dirty());
}

TEST_F(GraphTest, ExplicitImplicit) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule catdep\n"
"  depfile = $out.d\n"
"  command = cat $in > $out\n"
"build implicit.h: cat data\n"
"build out.o: catdep foo.cc || implicit.h\n"));
  fs_.Create("implicit.h", "");
  fs_.Create("foo.cc", "");
  fs_.Create("out.o.d", "out.o: implicit.h\n");
  fs_.Create("out.o", "");
  fs_.Tick();
  fs_.Create("data", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out.o"), &err));
  ASSERT_EQ("", err);

  // We have both an implicit and an explicit dep on implicit.h.
  // The implicit dep should "win" (in the sense that it should cause
  // the output to be dirty).
  EXPECT_TRUE(GetNode("out.o")->dirty());
}

TEST_F(GraphTest, ImplicitOutputParse) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out | out.imp: cat in\n"));

  Edge* edge = GetNode("out")->in_edge();
  EXPECT_EQ(2, edge->outputs_.size());
  EXPECT_EQ("out", edge->outputs_[0]->path());
  EXPECT_EQ("out.imp", edge->outputs_[1]->path());
  EXPECT_EQ(1, edge->implicit_outs_);
  EXPECT_EQ(edge, GetNode("out.imp")->in_edge());
}

TEST_F(GraphTest, ImplicitOutputMissing) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out | out.imp: cat in\n"));
  fs_.Create("in", "");
  fs_.Create("out", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out"), &err));
  ASSERT_EQ("", err);

  EXPECT_TRUE(GetNode("out")->dirty());
  EXPECT_TRUE(GetNode("out.imp")->dirty());
}

TEST_F(GraphTest, ImplicitOutputOutOfDate) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out | out.imp: cat in\n"));
  fs_.Create("out.imp", "");
  fs_.Tick();
  fs_.Create("in", "");
  fs_.Create("out", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out"), &err));
  ASSERT_EQ("", err);

  EXPECT_TRUE(GetNode("out")->dirty());
  EXPECT_TRUE(GetNode("out.imp")->dirty());
}

TEST_F(GraphTest, ImplicitOutputOnlyParse) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build | out.imp: cat in\n"));

  Edge* edge = GetNode("out.imp")->in_edge();
  EXPECT_EQ(1, edge->outputs_.size());
  EXPECT_EQ("out.imp", edge->outputs_[0]->path());
  EXPECT_EQ(1, edge->implicit_outs_);
  EXPECT_EQ(edge, GetNode("out.imp")->in_edge());
}

TEST_F(GraphTest, ImplicitOutputOnlyMissing) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build | out.imp: cat in\n"));
  fs_.Create("in", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out.imp"), &err));
  ASSERT_EQ("", err);

  EXPECT_TRUE(GetNode("out.imp")->dirty());
}

TEST_F(GraphTest, ImplicitOutputOnlyOutOfDate) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build | out.imp: cat in\n"));
  fs_.Create("out.imp", "");
  fs_.Tick();
  fs_.Create("in", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out.imp"), &err));
  ASSERT_EQ("", err);

  EXPECT_TRUE(GetNode("out.imp")->dirty());
}

TEST_F(GraphTest, PathWithCurrentDirectory) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule catdep\n"
"  depfile = $out.d\n"
"  command = cat $in > $out\n"
"build ./out.o: catdep ./foo.cc\n"));
  fs_.Create("foo.cc", "");
  fs_.Create("out.o.d", "out.o: foo.cc\n");
  fs_.Create("out.o", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out.o"), &err));
  ASSERT_EQ("", err);

  EXPECT_FALSE(GetNode("out.o")->dirty());
}

TEST_F(GraphTest, RootNodes) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out1: cat in1\n"
"build mid1: cat in1\n"
"build out2: cat mid1\n"
"build out3 out4: cat mid1\n"));

  string err;
  vector<Node*> root_nodes = state_.RootNodes(&err);
  EXPECT_EQ(4u, root_nodes.size());
  for (size_t i = 0; i < root_nodes.size(); ++i) {
    string name = root_nodes[i]->path();
    EXPECT_EQ("out", name.substr(0, 3));
  }
}

TEST_F(GraphTest, VarInOutPathEscaping) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build a$ b: cat no'space with$ space$$ no\"space2\n"));

  Edge* edge = GetNode("a b")->in_edge();
#if _WIN32
  EXPECT_EQ("cat no'space \"with space$\" \"no\\\"space2\" > \"a b\"",
      edge->EvaluateCommand());
#else
  EXPECT_EQ("cat 'no'\\''space' 'with space$' 'no\"space2' > 'a b'",
      edge->EvaluateCommand());
#endif
}

// Regression test for https://github.com/ninja-build/ninja/issues/380
TEST_F(GraphTest, DepfileWithCanonicalizablePath) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule catdep\n"
"  depfile = $out.d\n"
"  command = cat $in > $out\n"
"build ./out.o: catdep ./foo.cc\n"));
  fs_.Create("foo.cc", "");
  fs_.Create("out.o.d", "out.o: bar/../foo.cc\n");
  fs_.Create("out.o", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out.o"), &err));
  ASSERT_EQ("", err);

  EXPECT_FALSE(GetNode("out.o")->dirty());
}

// Regression test for https://github.com/ninja-build/ninja/issues/404
TEST_F(GraphTest, DepfileRemoved) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule catdep\n"
"  depfile = $out.d\n"
"  command = cat $in > $out\n"
"build ./out.o: catdep ./foo.cc\n"));
  fs_.Create("foo.h", "");
  fs_.Create("foo.cc", "");
  fs_.Tick();
  fs_.Create("out.o.d", "out.o: foo.h\n");
  fs_.Create("out.o", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out.o"), &err));
  ASSERT_EQ("", err);
  EXPECT_FALSE(GetNode("out.o")->dirty());

  state_.Reset();
  fs_.RemoveFile("out.o.d");
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out.o"), &err));
  ASSERT_EQ("", err);
  EXPECT_TRUE(GetNode("out.o")->dirty());
}

// Check that rule-level variables are in scope for eval.
TEST_F(GraphTest, RuleVariablesInScope) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule r\n"
"  depfile = x\n"
"  command = depfile is $depfile\n"
"build out: r in\n"));
  Edge* edge = GetNode("out")->in_edge();
  EXPECT_EQ("depfile is x", edge->EvaluateCommand());
}

// Check that build statements can override rule builtins like depfile.
TEST_F(GraphTest, DepfileOverride) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule r\n"
"  depfile = x\n"
"  command = unused\n"
"build out: r in\n"
"  depfile = y\n"));
  Edge* edge = GetNode("out")->in_edge();
  EXPECT_EQ("y", edge->GetBinding("depfile"));
}

// Check that overridden values show up in expansion of rule-level bindings.
TEST_F(GraphTest, DepfileOverrideParent) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule r\n"
"  depfile = x\n"
"  command = depfile is $depfile\n"
"build out: r in\n"
"  depfile = y\n"));
  Edge* edge = GetNode("out")->in_edge();
  EXPECT_EQ("depfile is y", edge->GetBinding("command"));
}

// Verify that building a nested phony rule prints "no work to do"
TEST_F(GraphTest, NestedPhonyPrintsDone) {
  AssertParse(&state_,
"build n1: phony \n"
"build n2: phony n1\n"
  );
  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("n2"), &err));
  ASSERT_EQ("", err);

  Plan plan_;
  EXPECT_TRUE(plan_.AddTarget(GetNode("n2"), &err));
  ASSERT_EQ("", err);

  EXPECT_EQ(0, plan_.command_edge_count());
  ASSERT_FALSE(plan_.more_to_do());
}

TEST_F(GraphTest, PhonySelfReferenceError) {
  ManifestParserOptions parser_opts;
  parser_opts.phony_cycle_action_ = kPhonyCycleActionError;
  AssertParse(&state_,
"build a: phony a\n",
  parser_opts);

  string err;
  EXPECT_FALSE(scan_.RecomputeDirty(GetNode("a"), &err));
  ASSERT_EQ("dependency cycle: a -> a [-w phonycycle=err]", err);
}

TEST_F(GraphTest, DependencyCycle) {
  AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n"
"build in: cat pre\n"
"build pre: cat out\n");

  string err;
  EXPECT_FALSE(scan_.RecomputeDirty(GetNode("out"), &err));
  ASSERT_EQ("dependency cycle: out -> mid -> in -> pre -> out", err);
}

TEST_F(GraphTest, CycleInEdgesButNotInNodes1) {
  string err;
  AssertParse(&state_,
"build a b: cat a\n");
  EXPECT_FALSE(scan_.RecomputeDirty(GetNode("b"), &err));
  ASSERT_EQ("dependency cycle: a -> a", err);
}

TEST_F(GraphTest, CycleInEdgesButNotInNodes2) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build b a: cat a\n"));
  EXPECT_FALSE(scan_.RecomputeDirty(GetNode("b"), &err));
  ASSERT_EQ("dependency cycle: a -> a", err);
}

TEST_F(GraphTest, CycleInEdgesButNotInNodes3) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build a b: cat c\n"
"build c: cat a\n"));
  EXPECT_FALSE(scan_.RecomputeDirty(GetNode("b"), &err));
  ASSERT_EQ("dependency cycle: a -> c -> a", err);
}

TEST_F(GraphTest, CycleInEdgesButNotInNodes4) {
  string err;
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build d: cat c\n"
"build c: cat b\n"
"build b: cat a\n"
"build a e: cat d\n"
"build f: cat e\n"));
  EXPECT_FALSE(scan_.RecomputeDirty(GetNode("f"), &err));
  ASSERT_EQ("dependency cycle: a -> d -> c -> b -> a", err);
}

// Verify that cycles in graphs with multiple outputs are handled correctly
// in RecomputeDirty() and don't cause deps to be loaded multiple times.
TEST_F(GraphTest, CycleWithLengthZeroFromDepfile) {
  AssertParse(&state_,
"rule deprule\n"
"   depfile = dep.d\n"
"   command = unused\n"
"build a b: deprule\n"
  );
  fs_.Create("dep.d", "a: b\n");

  string err;
  EXPECT_FALSE(scan_.RecomputeDirty(GetNode("a"), &err));
  ASSERT_EQ("dependency cycle: b -> b", err);

  // Despite the depfile causing edge to be a cycle (it has outputs a and b,
  // but the depfile also adds b as an input), the deps should have been loaded
  // only once:
  Edge* edge = GetNode("a")->in_edge();
  EXPECT_EQ(1, edge->inputs_.size());
  EXPECT_EQ("b", edge->inputs_[0]->path());
}

// Like CycleWithLengthZeroFromDepfile but with a higher cycle length.
TEST_F(GraphTest, CycleWithLengthOneFromDepfile) {
  AssertParse(&state_,
"rule deprule\n"
"   depfile = dep.d\n"
"   command = unused\n"
"rule r\n"
"   command = unused\n"
"build a b: deprule\n"
"build c: r b\n"
  );
  fs_.Create("dep.d", "a: c\n");

  string err;
  EXPECT_FALSE(scan_.RecomputeDirty(GetNode("a"), &err));
  ASSERT_EQ("dependency cycle: b -> c -> b", err);

  // Despite the depfile causing edge to be a cycle (|edge| has outputs a and b,
  // but c's in_edge has b as input but the depfile also adds |edge| as
  // output)), the deps should have been loaded only once:
  Edge* edge = GetNode("a")->in_edge();
  EXPECT_EQ(1, edge->inputs_.size());
  EXPECT_EQ("c", edge->inputs_[0]->path());
}

// Like CycleWithLengthOneFromDepfile but building a node one hop away from
// the cycle.
TEST_F(GraphTest, CycleWithLengthOneFromDepfileOneHopAway) {
  AssertParse(&state_,
"rule deprule\n"
"   depfile = dep.d\n"
"   command = unused\n"
"rule r\n"
"   command = unused\n"
"build a b: deprule\n"
"build c: r b\n"
"build d: r a\n"
  );
  fs_.Create("dep.d", "a: c\n");

  string err;
  EXPECT_FALSE(scan_.RecomputeDirty(GetNode("d"), &err));
  ASSERT_EQ("dependency cycle: b -> c -> b", err);

  // Despite the depfile causing edge to be a cycle (|edge| has outputs a and b,
  // but c's in_edge has b as input but the depfile also adds |edge| as
  // output)), the deps should have been loaded only once:
  Edge* edge = GetNode("a")->in_edge();
  EXPECT_EQ(1, edge->inputs_.size());
  EXPECT_EQ("c", edge->inputs_[0]->path());
}

#ifdef _WIN32
TEST_F(GraphTest, Decanonicalize) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out\\out1: cat src\\in1\n"
"build out\\out2/out3\\out4: cat mid1\n"
"build out3 out4\\foo: cat mid1\n"));

  string err;
  vector<Node*> root_nodes = state_.RootNodes(&err);
  EXPECT_EQ(4u, root_nodes.size());
  EXPECT_EQ(root_nodes[0]->path(), "out/out1");
  EXPECT_EQ(root_nodes[1]->path(), "out/out2/out3/out4");
  EXPECT_EQ(root_nodes[2]->path(), "out3");
  EXPECT_EQ(root_nodes[3]->path(), "out4/foo");
  EXPECT_EQ(root_nodes[0]->PathDecanonicalized(), "out\\out1");
  EXPECT_EQ(root_nodes[1]->PathDecanonicalized(), "out\\out2/out3\\out4");
  EXPECT_EQ(root_nodes[2]->PathDecanonicalized(), "out3");
  EXPECT_EQ(root_nodes[3]->PathDecanonicalized(), "out4\\foo");
}
#endif
