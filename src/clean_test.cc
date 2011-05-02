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

#include "clean.h"
#include "build.h"

#include "test.h"

struct CleanTest : public StateTestWithBuiltinRules {
  VirtualFileSystem fs_;
  BuildConfig config_;
  virtual void SetUp() {
    config_.verbosity = BuildConfig::QUIET;
  }
};

TEST_F(CleanTest, CleanAll) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build in1: cat src1\n"
"build out1: cat in1\n"
"build in2: cat src2\n"
"build out2: cat in2\n"));
  fs_.Create("in1", 1, "");
  fs_.Create("out1", 1, "");
  fs_.Create("in2", 1, "");
  fs_.Create("out2", 1, "");

  Cleaner cleaner(&state_, config_, &fs_);

  ASSERT_EQ(0, cleaner.cleaned_files_count());
  EXPECT_EQ(0, cleaner.CleanAll());
  EXPECT_EQ(4, cleaner.cleaned_files_count());
  EXPECT_EQ(4, fs_.files_removed_.size());

  // Check they are removed.
  EXPECT_EQ(0, fs_.Stat("in1"));
  EXPECT_EQ(0, fs_.Stat("out1"));
  EXPECT_EQ(0, fs_.Stat("in2"));
  EXPECT_EQ(0, fs_.Stat("out2"));
  fs_.files_removed_.clear();

  EXPECT_EQ(0, cleaner.CleanAll());
  EXPECT_EQ(0, cleaner.cleaned_files_count());
  EXPECT_EQ(0, fs_.files_removed_.size());
}

TEST_F(CleanTest, CleanAllDryRun) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build in1: cat src1\n"
"build out1: cat in1\n"
"build in2: cat src2\n"
"build out2: cat in2\n"));
  fs_.Create("in1", 1, "");
  fs_.Create("out1", 1, "");
  fs_.Create("in2", 1, "");
  fs_.Create("out2", 1, "");

  config_.dry_run = true;
  Cleaner cleaner(&state_, config_, &fs_);

  ASSERT_EQ(0, cleaner.cleaned_files_count());
  EXPECT_EQ(0, cleaner.CleanAll());
  EXPECT_EQ(4, cleaner.cleaned_files_count());
  EXPECT_EQ(0, fs_.files_removed_.size());

  // Check they are not removed.
  EXPECT_NE(0, fs_.Stat("in1"));
  EXPECT_NE(0, fs_.Stat("out1"));
  EXPECT_NE(0, fs_.Stat("in2"));
  EXPECT_NE(0, fs_.Stat("out2"));
  fs_.files_removed_.clear();

  EXPECT_EQ(0, cleaner.CleanAll());
  EXPECT_EQ(4, cleaner.cleaned_files_count());
  EXPECT_EQ(0, fs_.files_removed_.size());
}

TEST_F(CleanTest, CleanTarget) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build in1: cat src1\n"
"build out1: cat in1\n"
"build in2: cat src2\n"
"build out2: cat in2\n"));
  fs_.Create("in1", 1, "");
  fs_.Create("out1", 1, "");
  fs_.Create("in2", 1, "");
  fs_.Create("out2", 1, "");

  Cleaner cleaner(&state_, config_, &fs_);

  ASSERT_EQ(0, cleaner.cleaned_files_count());
  ASSERT_EQ(0, cleaner.CleanTarget("out1"));
  EXPECT_EQ(2, cleaner.cleaned_files_count());
  EXPECT_EQ(2, fs_.files_removed_.size());

  // Check they are removed.
  EXPECT_EQ(0, fs_.Stat("in1"));
  EXPECT_EQ(0, fs_.Stat("out1"));
  EXPECT_NE(0, fs_.Stat("in2"));
  EXPECT_NE(0, fs_.Stat("out2"));
  fs_.files_removed_.clear();

  ASSERT_EQ(0, cleaner.CleanTarget("out1"));
  EXPECT_EQ(0, cleaner.cleaned_files_count());
  EXPECT_EQ(0, fs_.files_removed_.size());
}

TEST_F(CleanTest, CleanTargetDryRun) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"build in1: cat src1\n"
"build out1: cat in1\n"
"build in2: cat src2\n"
"build out2: cat in2\n"));
  fs_.Create("in1", 1, "");
  fs_.Create("out1", 1, "");
  fs_.Create("in2", 1, "");
  fs_.Create("out2", 1, "");

  config_.dry_run = true;
  Cleaner cleaner(&state_, config_, &fs_);

  ASSERT_EQ(0, cleaner.cleaned_files_count());
  ASSERT_EQ(0, cleaner.CleanTarget("out1"));
  EXPECT_EQ(2, cleaner.cleaned_files_count());
  EXPECT_EQ(0, fs_.files_removed_.size());

  // Check they are removed.
  EXPECT_NE(0, fs_.Stat("in1"));
  EXPECT_NE(0, fs_.Stat("out1"));
  EXPECT_NE(0, fs_.Stat("in2"));
  EXPECT_NE(0, fs_.Stat("out2"));
  fs_.files_removed_.clear();

  ASSERT_EQ(0, cleaner.CleanTarget("out1"));
  EXPECT_EQ(2, cleaner.cleaned_files_count());
  EXPECT_EQ(0, fs_.files_removed_.size());
}

TEST_F(CleanTest, CleanRule) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cat_e\n"
"  command = cat -e $in > $out\n"
"build in1: cat_e src1\n"
"build out1: cat in1\n"
"build in2: cat_e src2\n"
"build out2: cat in2\n"));
  fs_.Create("in1", 1, "");
  fs_.Create("out1", 1, "");
  fs_.Create("in2", 1, "");
  fs_.Create("out2", 1, "");

  Cleaner cleaner(&state_, config_, &fs_);

  ASSERT_EQ(0, cleaner.cleaned_files_count());
  ASSERT_EQ(0, cleaner.CleanRule("cat_e"));
  EXPECT_EQ(2, cleaner.cleaned_files_count());
  EXPECT_EQ(2, fs_.files_removed_.size());

  // Check they are removed.
  EXPECT_EQ(0, fs_.Stat("in1"));
  EXPECT_NE(0, fs_.Stat("out1"));
  EXPECT_EQ(0, fs_.Stat("in2"));
  EXPECT_NE(0, fs_.Stat("out2"));
  fs_.files_removed_.clear();

  ASSERT_EQ(0, cleaner.CleanRule("cat_e"));
  EXPECT_EQ(0, cleaner.cleaned_files_count());
  EXPECT_EQ(0, fs_.files_removed_.size());
}

TEST_F(CleanTest, CleanRuleDryRun) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
"rule cat_e\n"
"  command = cat -e $in > $out\n"
"build in1: cat_e src1\n"
"build out1: cat in1\n"
"build in2: cat_e src2\n"
"build out2: cat in2\n"));
  fs_.Create("in1", 1, "");
  fs_.Create("out1", 1, "");
  fs_.Create("in2", 1, "");
  fs_.Create("out2", 1, "");

  config_.dry_run = true;
  Cleaner cleaner(&state_, config_, &fs_);

  ASSERT_EQ(0, cleaner.cleaned_files_count());
  ASSERT_EQ(0, cleaner.CleanRule("cat_e"));
  EXPECT_EQ(2, cleaner.cleaned_files_count());
  EXPECT_EQ(0, fs_.files_removed_.size());

  // Check they are removed.
  EXPECT_NE(0, fs_.Stat("in1"));
  EXPECT_NE(0, fs_.Stat("out1"));
  EXPECT_NE(0, fs_.Stat("in2"));
  EXPECT_NE(0, fs_.Stat("out2"));
  fs_.files_removed_.clear();

  ASSERT_EQ(0, cleaner.CleanRule("cat_e"));
  EXPECT_EQ(2, cleaner.cleaned_files_count());
  EXPECT_EQ(0, fs_.files_removed_.size());
}

TEST_F(CleanTest, CleanFailure) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
                                      "build dir: cat src1\n"));
  fs_.MakeDir("dir");
  Cleaner cleaner(&state_, config_, &fs_);
  EXPECT_NE(0, cleaner.CleanAll());
}
