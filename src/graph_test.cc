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
  GraphTest() : scan_(&state_, NULL, NULL, &fs_, NULL) {}

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
#ifdef _WIN32
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

TEST_F(GraphTest, DyndepLoadTrivial) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build out: r in || dd\n"
"  dyndep = dd\n"
  );
  fs_.Create("dd",
"ninja_dyndep_version = 1\n"
"build out: dyndep\n"
  );

  string err;
  ASSERT_TRUE(GetNode("dd")->dyndep_pending());
  EXPECT_TRUE(scan_.LoadDyndeps(GetNode("dd"), &err));
  EXPECT_EQ("", err);
  EXPECT_FALSE(GetNode("dd")->dyndep_pending());

  Edge* edge = GetNode("out")->in_edge();
  ASSERT_EQ(1u, edge->outputs_.size());
  EXPECT_EQ("out", edge->outputs_[0]->path());
  ASSERT_EQ(2u, edge->inputs_.size());
  EXPECT_EQ("in", edge->inputs_[0]->path());
  EXPECT_EQ("dd", edge->inputs_[1]->path());
  EXPECT_EQ(0u, edge->implicit_deps_);
  EXPECT_EQ(1u, edge->order_only_deps_);
  EXPECT_FALSE(edge->GetBindingBool("restat"));
}

TEST_F(GraphTest, DyndepLoadMissingFile) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build out: r in || dd\n"
"  dyndep = dd\n"
  );

  string err;
  ASSERT_TRUE(GetNode("dd")->dyndep_pending());
  EXPECT_FALSE(scan_.LoadDyndeps(GetNode("dd"), &err));
  EXPECT_EQ("loading 'dd': No such file or directory", err);
}

TEST_F(GraphTest, DyndepLoadMissingEntry) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build out: r in || dd\n"
"  dyndep = dd\n"
  );
  fs_.Create("dd",
"ninja_dyndep_version = 1\n"
  );

  string err;
  ASSERT_TRUE(GetNode("dd")->dyndep_pending());
  EXPECT_FALSE(scan_.LoadDyndeps(GetNode("dd"), &err));
  EXPECT_EQ("'out' not mentioned in its dyndep file 'dd'", err);
}

TEST_F(GraphTest, DyndepLoadExtraEntry) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build out: r in || dd\n"
"  dyndep = dd\n"
"build out2: r in || dd\n"
  );
  fs_.Create("dd",
"ninja_dyndep_version = 1\n"
"build out: dyndep\n"
"build out2: dyndep\n"
  );

  string err;
  ASSERT_TRUE(GetNode("dd")->dyndep_pending());
  EXPECT_FALSE(scan_.LoadDyndeps(GetNode("dd"), &err));
  EXPECT_EQ("dyndep file 'dd' mentions output 'out2' whose build statement "
            "does not have a dyndep binding for the file", err);
}

TEST_F(GraphTest, DyndepLoadOutputWithMultipleRules1) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build out1 | out-twice.imp: r in1\n"
"build out2: r in2 || dd\n"
"  dyndep = dd\n"
  );
  fs_.Create("dd",
"ninja_dyndep_version = 1\n"
"build out2 | out-twice.imp: dyndep\n"
  );

  string err;
  ASSERT_TRUE(GetNode("dd")->dyndep_pending());
  EXPECT_FALSE(scan_.LoadDyndeps(GetNode("dd"), &err));
  EXPECT_EQ("multiple rules generate out-twice.imp", err);
}

TEST_F(GraphTest, DyndepLoadOutputWithMultipleRules2) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build out1: r in1 || dd1\n"
"  dyndep = dd1\n"
"build out2: r in2 || dd2\n"
"  dyndep = dd2\n"
  );
  fs_.Create("dd1",
"ninja_dyndep_version = 1\n"
"build out1 | out-twice.imp: dyndep\n"
  );
  fs_.Create("dd2",
"ninja_dyndep_version = 1\n"
"build out2 | out-twice.imp: dyndep\n"
  );

  string err;
  ASSERT_TRUE(GetNode("dd1")->dyndep_pending());
  EXPECT_TRUE(scan_.LoadDyndeps(GetNode("dd1"), &err));
  EXPECT_EQ("", err);
  ASSERT_TRUE(GetNode("dd2")->dyndep_pending());
  EXPECT_FALSE(scan_.LoadDyndeps(GetNode("dd2"), &err));
  EXPECT_EQ("multiple rules generate out-twice.imp", err);
}

TEST_F(GraphTest, DyndepLoadMultiple) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build out1: r in1 || dd\n"
"  dyndep = dd\n"
"build out2: r in2 || dd\n"
"  dyndep = dd\n"
"build outNot: r in3 || dd\n"
  );
  fs_.Create("dd",
"ninja_dyndep_version = 1\n"
"build out1 | out1imp: dyndep | in1imp\n"
"build out2: dyndep | in2imp\n"
"  restat = 1\n"
  );

  string err;
  ASSERT_TRUE(GetNode("dd")->dyndep_pending());
  EXPECT_TRUE(scan_.LoadDyndeps(GetNode("dd"), &err));
  EXPECT_EQ("", err);
  EXPECT_FALSE(GetNode("dd")->dyndep_pending());

  Edge* edge1 = GetNode("out1")->in_edge();
  ASSERT_EQ(2u, edge1->outputs_.size());
  EXPECT_EQ("out1", edge1->outputs_[0]->path());
  EXPECT_EQ("out1imp", edge1->outputs_[1]->path());
  EXPECT_EQ(1u, edge1->implicit_outs_);
  ASSERT_EQ(3u, edge1->inputs_.size());
  EXPECT_EQ("in1", edge1->inputs_[0]->path());
  EXPECT_EQ("in1imp", edge1->inputs_[1]->path());
  EXPECT_EQ("dd", edge1->inputs_[2]->path());
  EXPECT_EQ(1u, edge1->implicit_deps_);
  EXPECT_EQ(1u, edge1->order_only_deps_);
  EXPECT_FALSE(edge1->GetBindingBool("restat"));
  EXPECT_EQ(edge1, GetNode("out1imp")->in_edge());
  Node* in1imp = GetNode("in1imp");
  ASSERT_EQ(1u, in1imp->out_edges().size());
  EXPECT_EQ(edge1, in1imp->out_edges()[0]);

  Edge* edge2 = GetNode("out2")->in_edge();
  ASSERT_EQ(1u, edge2->outputs_.size());
  EXPECT_EQ("out2", edge2->outputs_[0]->path());
  EXPECT_EQ(0u, edge2->implicit_outs_);
  ASSERT_EQ(3u, edge2->inputs_.size());
  EXPECT_EQ("in2", edge2->inputs_[0]->path());
  EXPECT_EQ("in2imp", edge2->inputs_[1]->path());
  EXPECT_EQ("dd", edge2->inputs_[2]->path());
  EXPECT_EQ(1u, edge2->implicit_deps_);
  EXPECT_EQ(1u, edge2->order_only_deps_);
  EXPECT_TRUE(edge2->GetBindingBool("restat"));
  Node* in2imp = GetNode("in2imp");
  ASSERT_EQ(1u, in2imp->out_edges().size());
  EXPECT_EQ(edge2, in2imp->out_edges()[0]);
}

TEST_F(GraphTest, DyndepFileMissing) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build out: r || dd\n"
"  dyndep = dd\n"
  );

  string err;
  EXPECT_FALSE(scan_.RecomputeDirty(GetNode("out"), &err));
  ASSERT_EQ("loading 'dd': No such file or directory", err);
}

TEST_F(GraphTest, DyndepFileError) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build out: r || dd\n"
"  dyndep = dd\n"
  );
  fs_.Create("dd",
"ninja_dyndep_version = 1\n"
  );

  string err;
  EXPECT_FALSE(scan_.RecomputeDirty(GetNode("out"), &err));
  ASSERT_EQ("'out' not mentioned in its dyndep file 'dd'", err);
}

TEST_F(GraphTest, DyndepImplicitInputNewer) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build out: r || dd\n"
"  dyndep = dd\n"
  );
  fs_.Create("dd",
"ninja_dyndep_version = 1\n"
"build out: dyndep | in\n"
  );
  fs_.Create("out", "");
  fs_.Tick();
  fs_.Create("in", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out"), &err));
  ASSERT_EQ("", err);

  EXPECT_FALSE(GetNode("in")->dirty());
  EXPECT_FALSE(GetNode("dd")->dirty());

  // "out" is dirty due to dyndep-specified implicit input
  EXPECT_TRUE(GetNode("out")->dirty());
}

TEST_F(GraphTest, DyndepFileReady) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build dd: r dd-in\n"
"build out: r || dd\n"
"  dyndep = dd\n"
  );
  fs_.Create("dd-in", "");
  fs_.Create("dd",
"ninja_dyndep_version = 1\n"
"build out: dyndep | in\n"
  );
  fs_.Create("out", "");
  fs_.Tick();
  fs_.Create("in", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out"), &err));
  ASSERT_EQ("", err);

  EXPECT_FALSE(GetNode("in")->dirty());
  EXPECT_FALSE(GetNode("dd")->dirty());
  EXPECT_TRUE(GetNode("dd")->in_edge()->outputs_ready());

  // "out" is dirty due to dyndep-specified implicit input
  EXPECT_TRUE(GetNode("out")->dirty());
}

TEST_F(GraphTest, DyndepFileNotClean) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build dd: r dd-in\n"
"build out: r || dd\n"
"  dyndep = dd\n"
  );
  fs_.Create("dd", "this-should-not-be-loaded");
  fs_.Tick();
  fs_.Create("dd-in", "");
  fs_.Create("out", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out"), &err));
  ASSERT_EQ("", err);

  EXPECT_TRUE(GetNode("dd")->dirty());
  EXPECT_FALSE(GetNode("dd")->in_edge()->outputs_ready());

  // "out" is clean but not ready since "dd" is not ready
  EXPECT_FALSE(GetNode("out")->dirty());
  EXPECT_FALSE(GetNode("out")->in_edge()->outputs_ready());
}

TEST_F(GraphTest, DyndepFileNotReady) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build tmp: r\n"
"build dd: r dd-in || tmp\n"
"build out: r || dd\n"
"  dyndep = dd\n"
  );
  fs_.Create("dd", "this-should-not-be-loaded");
  fs_.Create("dd-in", "");
  fs_.Tick();
  fs_.Create("out", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out"), &err));
  ASSERT_EQ("", err);

  EXPECT_FALSE(GetNode("dd")->dirty());
  EXPECT_FALSE(GetNode("dd")->in_edge()->outputs_ready());
  EXPECT_FALSE(GetNode("out")->dirty());
  EXPECT_FALSE(GetNode("out")->in_edge()->outputs_ready());
}

TEST_F(GraphTest, DyndepFileSecondNotReady) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build dd1: r dd1-in\n"
"build dd2-in: r || dd1\n"
"  dyndep = dd1\n"
"build dd2: r dd2-in\n"
"build out: r || dd2\n"
"  dyndep = dd2\n"
  );
  fs_.Create("dd1", "");
  fs_.Create("dd2", "");
  fs_.Create("dd2-in", "");
  fs_.Tick();
  fs_.Create("dd1-in", "");
  fs_.Create("out", "");

  string err;
  EXPECT_TRUE(scan_.RecomputeDirty(GetNode("out"), &err));
  ASSERT_EQ("", err);

  EXPECT_TRUE(GetNode("dd1")->dirty());
  EXPECT_FALSE(GetNode("dd1")->in_edge()->outputs_ready());
  EXPECT_FALSE(GetNode("dd2")->dirty());
  EXPECT_FALSE(GetNode("dd2")->in_edge()->outputs_ready());
  EXPECT_FALSE(GetNode("out")->dirty());
  EXPECT_FALSE(GetNode("out")->in_edge()->outputs_ready());
}

TEST_F(GraphTest, DyndepFileCircular) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build out: r in || dd\n"
"  depfile = out.d\n"
"  dyndep = dd\n"
"build in: r circ\n"
  );
  fs_.Create("out.d", "out: inimp\n");
  fs_.Create("dd",
"ninja_dyndep_version = 1\n"
"build out | circ: dyndep\n"
  );
  fs_.Create("out", "");

  Edge* edge = GetNode("out")->in_edge();
  string err;
  EXPECT_FALSE(scan_.RecomputeDirty(GetNode("out"), &err));
  EXPECT_EQ("dependency cycle: circ -> in -> circ", err);

  // Verify that "out.d" was loaded exactly once despite
  // circular reference discovered from dyndep file.
  ASSERT_EQ(3u, edge->inputs_.size());
  EXPECT_EQ("in", edge->inputs_[0]->path());
  EXPECT_EQ("inimp", edge->inputs_[1]->path());
  EXPECT_EQ("dd", edge->inputs_[2]->path());
  EXPECT_EQ(1u, edge->implicit_deps_);
  EXPECT_EQ(1u, edge->order_only_deps_);
}
