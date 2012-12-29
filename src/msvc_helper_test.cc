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

#include "msvc_helper.h"

#include <gtest/gtest.h>

#include "test.h"
#include "util.h"

TEST(MSVCHelperTest, ShowIncludes) {
  ASSERT_EQ("", CLWrapper::FilterShowIncludes(""));

  ASSERT_EQ("", CLWrapper::FilterShowIncludes("Sample compiler output"));
  ASSERT_EQ("c:\\Some Files\\foobar.h",
            CLWrapper::FilterShowIncludes("Note: including file: "
                                          "c:\\Some Files\\foobar.h"));
  ASSERT_EQ("c:\\initspaces.h",
            CLWrapper::FilterShowIncludes("Note: including file:    "
                                          "c:\\initspaces.h"));
}

TEST(MSVCHelperTest, FilterInputFilename) {
  ASSERT_TRUE(CLWrapper::FilterInputFilename("foobar.cc"));
  ASSERT_TRUE(CLWrapper::FilterInputFilename("foo bar.cc"));
  ASSERT_TRUE(CLWrapper::FilterInputFilename("baz.c"));

  ASSERT_FALSE(CLWrapper::FilterInputFilename(
                   "src\\cl_helper.cc(166) : fatal error C1075: end "
                   "of file found ..."));
}

TEST(MSVCHelperTest, Run) {
  CLWrapper cl;
  string output;
  cl.Run("cmd /c \"echo foo&& echo Note: including file:  foo.h&&echo bar\"",
         &output);
  ASSERT_EQ("foo\nbar\n", output);
  ASSERT_EQ(1u, cl.includes_.size());
  ASSERT_EQ("foo.h", *cl.includes_.begin());
}

TEST(MSVCHelperTest, RunFilenameFilter) {
  CLWrapper cl;
  string output;
  cl.Run("cmd /c \"echo foo.cc&& echo cl: warning\"",
         &output);
  ASSERT_EQ("cl: warning\n", output);
}

TEST(MSVCHelperTest, RunSystemInclude) {
  CLWrapper cl;
  string output;
  cl.Run("cmd /c \"echo Note: including file: c:\\Program Files\\foo.h&&"
         "echo Note: including file: d:\\Microsoft Visual Studio\\bar.h&&"
         "echo Note: including file: path.h\"",
         &output);
  // We should have dropped the first two includes because they look like
  // system headers.
  ASSERT_EQ("", output);
  ASSERT_EQ(1u, cl.includes_.size());
  ASSERT_EQ("path.h", *cl.includes_.begin());
}

TEST(MSVCHelperTest, EnvBlock) {
  char env_block[] = "foo=bar\0";
  CLWrapper cl;
  cl.SetEnvBlock(env_block);
  string output;
  cl.Run("cmd /c \"echo foo is %foo%", &output);
  ASSERT_EQ("foo is bar\n", output);
}

TEST(MSVCHelperTest, DuplicatedHeader) {
  CLWrapper cl;
  string output;
  cl.Run("cmd /c \"echo Note: including file: foo.h&&"
         "echo Note: including file: bar.h&&"
         "echo Note: including file: foo.h\"",
         &output);
  // We should have dropped one copy of foo.h.
  ASSERT_EQ("", output);
  ASSERT_EQ(2u, cl.includes_.size());
}

TEST(MSVCHelperTest, DuplicatedHeaderPathConverted) {
  CLWrapper cl;
  string output;
  cl.Run("cmd /c \"echo Note: including file: sub/foo.h&&"
         "echo Note: including file: bar.h&&"
         "echo Note: including file: sub\\foo.h\"",
         &output);
  // We should have dropped one copy of foo.h.
  ASSERT_EQ("", output);
  ASSERT_EQ(2u, cl.includes_.size());
}

TEST(MSVCHelperTest, SpacesInFilename) {
  CLWrapper cl;
  string output;
  cl.Run("cmd /c \"echo Note: including file: sub\\some sdk\\foo.h",
         &output);
  ASSERT_EQ("", output);
  vector<string> headers = cl.GetEscapedResult();
  ASSERT_EQ(1u, headers.size());
  ASSERT_EQ("sub\\some\\ sdk\\foo.h", headers[0]);
}
