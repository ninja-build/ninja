// Copyright 2015 Google Inc. All Rights Reserved.
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

#include "dyndep_parser.h"

#include <map>
#include <vector>

#include "dyndep.h"
#include "graph.h"
#include "state.h"
#include "test.h"

struct DyndepParserTest : public testing::Test {
  void AssertParse(const char* input) {
    DyndepParser parser(&state_, &fs_, &dyndep_file_);
    string err;
    EXPECT_TRUE(parser.ParseTest(input, &err));
    ASSERT_EQ("", err);
  }

  virtual void SetUp() {
    ::AssertParse(&state_,
"rule touch\n"
"  command = touch $out\n"
"build out otherout: touch\n");
  }

  State state_;
  VirtualFileSystem fs_;
  DyndepFile dyndep_file_;
};

TEST_F(DyndepParserTest, Empty) {
  const char kInput[] =
"";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:1: expected 'ninja_dyndep_version = ...'\n", err);
}

TEST_F(DyndepParserTest, Version1) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1\n"));
}

TEST_F(DyndepParserTest, Version1Extra) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1-extra\n"));
}

TEST_F(DyndepParserTest, Version1_0) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1.0\n"));
}

TEST_F(DyndepParserTest, Version1_0Extra) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1.0-extra\n"));
}

TEST_F(DyndepParserTest, CommentVersion) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"# comment\n"
"ninja_dyndep_version = 1\n"));
}

TEST_F(DyndepParserTest, BlankLineVersion) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"\n"
"ninja_dyndep_version = 1\n"));
}

TEST_F(DyndepParserTest, VersionCRLF) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1\r\n"));
}

TEST_F(DyndepParserTest, CommentVersionCRLF) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"# comment\r\n"
"ninja_dyndep_version = 1\r\n"));
}

TEST_F(DyndepParserTest, BlankLineVersionCRLF) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"\r\n"
"ninja_dyndep_version = 1\r\n"));
}

TEST_F(DyndepParserTest, VersionUnexpectedEOF) {
  const char kInput[] =
"ninja_dyndep_version = 1.0";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:1: unexpected EOF\n"
            "ninja_dyndep_version = 1.0\n"
            "                          ^ near here", err);
}

TEST_F(DyndepParserTest, UnsupportedVersion0) {
  const char kInput[] =
"ninja_dyndep_version = 0\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:1: unsupported 'ninja_dyndep_version = 0'\n"
            "ninja_dyndep_version = 0\n"
            "                        ^ near here", err);
}

TEST_F(DyndepParserTest, UnsupportedVersion1_1) {
  const char kInput[] =
"ninja_dyndep_version = 1.1\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:1: unsupported 'ninja_dyndep_version = 1.1'\n"
            "ninja_dyndep_version = 1.1\n"
            "                          ^ near here", err);
}

TEST_F(DyndepParserTest, DuplicateVersion) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"ninja_dyndep_version = 1\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:2: unexpected identifier\n", err);
}

TEST_F(DyndepParserTest, MissingVersionOtherVar) {
  const char kInput[] =
"not_ninja_dyndep_version = 1\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:1: expected 'ninja_dyndep_version = ...'\n"
            "not_ninja_dyndep_version = 1\n"
            "                            ^ near here", err);
}

TEST_F(DyndepParserTest, MissingVersionBuild) {
  const char kInput[] =
"build out: dyndep\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:1: expected 'ninja_dyndep_version = ...'\n", err);
}

TEST_F(DyndepParserTest, UnexpectedEqual) {
  const char kInput[] =
"= 1\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:1: unexpected '='\n", err);
}

TEST_F(DyndepParserTest, UnexpectedIndent) {
  const char kInput[] =
" = 1\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:1: unexpected indent\n", err);
}

TEST_F(DyndepParserTest, OutDuplicate) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"build out: dyndep\n"
"build out: dyndep\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:3: multiple statements for 'out'\n"
            "build out: dyndep\n"
            "         ^ near here", err);
}

TEST_F(DyndepParserTest, OutDuplicateThroughOther) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"build out: dyndep\n"
"build otherout: dyndep\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:3: multiple statements for 'otherout'\n"
            "build otherout: dyndep\n"
            "              ^ near here", err);
}

TEST_F(DyndepParserTest, NoOutEOF) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"build";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:2: unexpected EOF\n"
            "build\n"
            "     ^ near here", err);
}

TEST_F(DyndepParserTest, NoOutColon) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"build :\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:2: expected path\n"
            "build :\n"
            "      ^ near here", err);
}

TEST_F(DyndepParserTest, OutNoStatement) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"build missing: dyndep\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:2: no build statement exists for 'missing'\n"
            "build missing: dyndep\n"
            "             ^ near here", err);
}

TEST_F(DyndepParserTest, OutEOF) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"build out";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:2: unexpected EOF\n"
            "build out\n"
            "         ^ near here", err);
}

TEST_F(DyndepParserTest, OutNoRule) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"build out:";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:2: expected build command name 'dyndep'\n"
            "build out:\n"
            "          ^ near here", err);
}

TEST_F(DyndepParserTest, OutBadRule) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"build out: touch";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:2: expected build command name 'dyndep'\n"
            "build out: touch\n"
            "           ^ near here", err);
}

TEST_F(DyndepParserTest, BuildEOF) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"build out: dyndep";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:2: unexpected EOF\n"
            "build out: dyndep\n"
            "                 ^ near here", err);
}

TEST_F(DyndepParserTest, ExplicitOut) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"build out exp: dyndep\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:2: explicit outputs not supported\n"
            "build out exp: dyndep\n"
            "             ^ near here", err);
}

TEST_F(DyndepParserTest, ExplicitIn) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"build out: dyndep exp\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:2: explicit inputs not supported\n"
            "build out: dyndep exp\n"
            "                     ^ near here", err);
}

TEST_F(DyndepParserTest, OrderOnlyIn) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"build out: dyndep ||\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:2: order-only inputs not supported\n"
            "build out: dyndep ||\n"
            "                  ^ near here", err);
}

TEST_F(DyndepParserTest, BadBinding) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"build out: dyndep\n"
"  not_restat = 1\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:3: binding is not 'restat'\n"
            "  not_restat = 1\n"
            "                ^ near here", err);
}

TEST_F(DyndepParserTest, RestatTwice) {
  const char kInput[] =
"ninja_dyndep_version = 1\n"
"build out: dyndep\n"
"  restat = 1\n"
"  restat = 1\n";
  DyndepParser parser(&state_, &fs_, &dyndep_file_);
  string err;
  EXPECT_FALSE(parser.ParseTest(kInput, &err));
  EXPECT_EQ("input:4: unexpected indent\n", err);
}

TEST_F(DyndepParserTest, NoImplicit) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1\n"
"build out: dyndep\n"));

  EXPECT_EQ(1u, dyndep_file_.size());
  DyndepFile::iterator i = dyndep_file_.find(state_.edges_[0]);
  ASSERT_NE(i, dyndep_file_.end());
  EXPECT_EQ(false, i->second.restat_);
  EXPECT_EQ(0u, i->second.implicit_outputs_.size());
  EXPECT_EQ(0u, i->second.implicit_inputs_.size());
}

TEST_F(DyndepParserTest, EmptyImplicit) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1\n"
"build out | : dyndep |\n"));

  EXPECT_EQ(1u, dyndep_file_.size());
  DyndepFile::iterator i = dyndep_file_.find(state_.edges_[0]);
  ASSERT_NE(i, dyndep_file_.end());
  EXPECT_EQ(false, i->second.restat_);
  EXPECT_EQ(0u, i->second.implicit_outputs_.size());
  EXPECT_EQ(0u, i->second.implicit_inputs_.size());
}

TEST_F(DyndepParserTest, ImplicitIn) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1\n"
"build out: dyndep | impin\n"));

  EXPECT_EQ(1u, dyndep_file_.size());
  DyndepFile::iterator i = dyndep_file_.find(state_.edges_[0]);
  ASSERT_NE(i, dyndep_file_.end());
  EXPECT_EQ(false, i->second.restat_);
  EXPECT_EQ(0u, i->second.implicit_outputs_.size());
  ASSERT_EQ(1u, i->second.implicit_inputs_.size());
  EXPECT_EQ("impin", i->second.implicit_inputs_[0]->path());
}

TEST_F(DyndepParserTest, ImplicitIns) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1\n"
"build out: dyndep | impin1 impin2\n"));

  EXPECT_EQ(1u, dyndep_file_.size());
  DyndepFile::iterator i = dyndep_file_.find(state_.edges_[0]);
  ASSERT_NE(i, dyndep_file_.end());
  EXPECT_EQ(false, i->second.restat_);
  EXPECT_EQ(0u, i->second.implicit_outputs_.size());
  ASSERT_EQ(2u, i->second.implicit_inputs_.size());
  EXPECT_EQ("impin1", i->second.implicit_inputs_[0]->path());
  EXPECT_EQ("impin2", i->second.implicit_inputs_[1]->path());
}

TEST_F(DyndepParserTest, ImplicitOut) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1\n"
"build out | impout: dyndep\n"));

  EXPECT_EQ(1u, dyndep_file_.size());
  DyndepFile::iterator i = dyndep_file_.find(state_.edges_[0]);
  ASSERT_NE(i, dyndep_file_.end());
  EXPECT_EQ(false, i->second.restat_);
  ASSERT_EQ(1u, i->second.implicit_outputs_.size());
  EXPECT_EQ("impout", i->second.implicit_outputs_[0]->path());
  EXPECT_EQ(0u, i->second.implicit_inputs_.size());
}

TEST_F(DyndepParserTest, ImplicitOuts) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1\n"
"build out | impout1 impout2 : dyndep\n"));

  EXPECT_EQ(1u, dyndep_file_.size());
  DyndepFile::iterator i = dyndep_file_.find(state_.edges_[0]);
  ASSERT_NE(i, dyndep_file_.end());
  EXPECT_EQ(false, i->second.restat_);
  ASSERT_EQ(2u, i->second.implicit_outputs_.size());
  EXPECT_EQ("impout1", i->second.implicit_outputs_[0]->path());
  EXPECT_EQ("impout2", i->second.implicit_outputs_[1]->path());
  EXPECT_EQ(0u, i->second.implicit_inputs_.size());
}

TEST_F(DyndepParserTest, ImplicitInsAndOuts) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1\n"
"build out | impout1 impout2: dyndep | impin1 impin2\n"));

  EXPECT_EQ(1u, dyndep_file_.size());
  DyndepFile::iterator i = dyndep_file_.find(state_.edges_[0]);
  ASSERT_NE(i, dyndep_file_.end());
  EXPECT_EQ(false, i->second.restat_);
  ASSERT_EQ(2u, i->second.implicit_outputs_.size());
  EXPECT_EQ("impout1", i->second.implicit_outputs_[0]->path());
  EXPECT_EQ("impout2", i->second.implicit_outputs_[1]->path());
  ASSERT_EQ(2u, i->second.implicit_inputs_.size());
  EXPECT_EQ("impin1", i->second.implicit_inputs_[0]->path());
  EXPECT_EQ("impin2", i->second.implicit_inputs_[1]->path());
}

TEST_F(DyndepParserTest, Restat) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1\n"
"build out: dyndep\n"
"  restat = 1\n"));

  EXPECT_EQ(1u, dyndep_file_.size());
  DyndepFile::iterator i = dyndep_file_.find(state_.edges_[0]);
  ASSERT_NE(i, dyndep_file_.end());
  EXPECT_EQ(true, i->second.restat_);
  EXPECT_EQ(0u, i->second.implicit_outputs_.size());
  EXPECT_EQ(0u, i->second.implicit_inputs_.size());
}

TEST_F(DyndepParserTest, OtherOutput) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1\n"
"build otherout: dyndep\n"));

  EXPECT_EQ(1u, dyndep_file_.size());
  DyndepFile::iterator i = dyndep_file_.find(state_.edges_[0]);
  ASSERT_NE(i, dyndep_file_.end());
  EXPECT_EQ(false, i->second.restat_);
  EXPECT_EQ(0u, i->second.implicit_outputs_.size());
  EXPECT_EQ(0u, i->second.implicit_inputs_.size());
}

TEST_F(DyndepParserTest, MultipleEdges) {
    ::AssertParse(&state_,
"build out2: touch\n");
  ASSERT_EQ(2u, state_.edges_.size());
  ASSERT_EQ(1u, state_.edges_[1]->outputs_.size());
  EXPECT_EQ("out2", state_.edges_[1]->outputs_[0]->path());
  EXPECT_EQ(0u, state_.edges_[0]->inputs_.size());

  ASSERT_NO_FATAL_FAILURE(AssertParse(
"ninja_dyndep_version = 1\n"
"build out: dyndep\n"
"build out2: dyndep\n"
"  restat = 1\n"));

  EXPECT_EQ(2u, dyndep_file_.size());
  {
    DyndepFile::iterator i = dyndep_file_.find(state_.edges_[0]);
    ASSERT_NE(i, dyndep_file_.end());
    EXPECT_EQ(false, i->second.restat_);
    EXPECT_EQ(0u, i->second.implicit_outputs_.size());
    EXPECT_EQ(0u, i->second.implicit_inputs_.size());
  }
  {
    DyndepFile::iterator i = dyndep_file_.find(state_.edges_[1]);
    ASSERT_NE(i, dyndep_file_.end());
    EXPECT_EQ(true, i->second.restat_);
    EXPECT_EQ(0u, i->second.implicit_outputs_.size());
    EXPECT_EQ(0u, i->second.implicit_inputs_.size());
  }
}
