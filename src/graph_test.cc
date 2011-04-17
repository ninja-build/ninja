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

#include "test.h"

struct GraphTest : public StateTestWithBuiltinRules {
  VirtualFileSystem fs_;
};

TEST_F(GraphTest, MissingImplicit) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build out: cat in | implicit\n"));
  fs_.Create("in", 1, "");
  fs_.Create("out", 1, "");

  Edge* edge = GetNode("out")->in_edge_;
  string err;
  EXPECT_TRUE(edge->RecomputeDirty(&state_, &fs_, &err));
  ASSERT_EQ("", err);

  // A missing implicit dep does not make the output dirty.
  EXPECT_FALSE(GetNode("out")->dirty_);
}

TEST_F(GraphTest, FunkyMakefilePath) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule catdep\n"
"  depfile = $out.d\n"
"  command = cat $in > $out\n"
"build out.o: catdep foo.cc\n"));
  fs_.Create("implicit.h", 2, "");
  fs_.Create("foo.cc", 1, "");
  fs_.Create("out.o.d", 1, "out.o: ./foo/../implicit.h\n");
  fs_.Create("out.o", 1, "");

  Edge* edge = GetNode("out.o")->in_edge_;
  string err;
  EXPECT_TRUE(edge->RecomputeDirty(&state_, &fs_, &err));
  ASSERT_EQ("", err);

  // implicit.h has changed, though our depfile refers to it with a
  // non-canonical path; we should still find it.
  EXPECT_TRUE(GetNode("out.o")->dirty_);
}

TEST_F(GraphTest, ExplicitImplicit) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule catdep\n"
"  depfile = $out.d\n"
"  command = cat $in > $out\n"
"build implicit.h: cat data\n"
"build out.o: catdep foo.cc || implicit.h\n"));
  fs_.Create("data", 2, "");
  fs_.Create("implicit.h", 1, "");
  fs_.Create("foo.cc", 1, "");
  fs_.Create("out.o.d", 1, "out.o: implicit.h\n");
  fs_.Create("out.o", 1, "");

  Edge* edge = GetNode("out.o")->in_edge_;
  string err;
  EXPECT_TRUE(edge->RecomputeDirty(&state_, &fs_, &err));
  ASSERT_EQ("", err);

  // We have both an implicit and an explicit dep on implicit.h.
  // The implicit dep should "win" (in the sense that it should cause
  // the output to be dirty).
  EXPECT_TRUE(GetNode("out.o")->dirty_);
}
