// Copyright 2012 Google Inc. All Rights Reserved.
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

#include "includes_normalize.h"

#include <direct.h>

#include "test.h"
#include "util.h"

TEST(IncludesNormalize, Simple) {
  EXPECT_EQ("b", IncludesNormalize::Normalize("a\\..\\b", NULL));
  EXPECT_EQ("b", IncludesNormalize::Normalize("a\\../b", NULL));
  EXPECT_EQ("a/b", IncludesNormalize::Normalize("a\\.\\b", NULL));
  EXPECT_EQ("a/b", IncludesNormalize::Normalize("a\\./b", NULL));
}

namespace {

string GetCurDir() {
  char buf[_MAX_PATH];
  _getcwd(buf, sizeof(buf));
  vector<string> parts = IncludesNormalize::Split(string(buf), '\\');
  return parts[parts.size() - 1];
}

}  // namespace

TEST(IncludesNormalize, WithRelative) {
  string currentdir = GetCurDir();
  EXPECT_EQ("c", IncludesNormalize::Normalize("a/b/c", "a/b"));
  EXPECT_EQ("a", IncludesNormalize::Normalize(IncludesNormalize::AbsPath("a"),
                                              NULL));
  EXPECT_EQ(string("../") + currentdir + string("/a"),
            IncludesNormalize::Normalize("a", "../b"));
  EXPECT_EQ(string("../") + currentdir + string("/a/b"),
            IncludesNormalize::Normalize("a/b", "../c"));
  EXPECT_EQ("../../a", IncludesNormalize::Normalize("a", "b/c"));
  EXPECT_EQ(".", IncludesNormalize::Normalize("a", "a"));
}

TEST(IncludesNormalize, Case) {
  EXPECT_EQ("b", IncludesNormalize::Normalize("Abc\\..\\b", NULL));
  EXPECT_EQ("BdEf", IncludesNormalize::Normalize("Abc\\..\\BdEf", NULL));
  EXPECT_EQ("A/b", IncludesNormalize::Normalize("A\\.\\b", NULL));
  EXPECT_EQ("a/b", IncludesNormalize::Normalize("a\\./b", NULL));
  EXPECT_EQ("A/B", IncludesNormalize::Normalize("A\\.\\B", NULL));
  EXPECT_EQ("A/B", IncludesNormalize::Normalize("A\\./B", NULL));
}

TEST(IncludesNormalize, Join) {
  vector<string> x;
  EXPECT_EQ("", IncludesNormalize::Join(x, ':'));
  x.push_back("alpha");
  EXPECT_EQ("alpha", IncludesNormalize::Join(x, ':'));
  x.push_back("beta");
  x.push_back("gamma");
  EXPECT_EQ("alpha:beta:gamma", IncludesNormalize::Join(x, ':'));
}

TEST(IncludesNormalize, Split) {
  EXPECT_EQ("", IncludesNormalize::Join(IncludesNormalize::Split("", '/'),
                                        ':'));
  EXPECT_EQ("a", IncludesNormalize::Join(IncludesNormalize::Split("a", '/'),
                                         ':'));
  EXPECT_EQ("a:b:c",
            IncludesNormalize::Join(
                IncludesNormalize::Split("a/b/c", '/'), ':'));
}

TEST(IncludesNormalize, ToLower) {
  EXPECT_EQ("", IncludesNormalize::ToLower(""));
  EXPECT_EQ("stuff", IncludesNormalize::ToLower("Stuff"));
  EXPECT_EQ("stuff and things", IncludesNormalize::ToLower("Stuff AND thINGS"));
  EXPECT_EQ("stuff 3and thin43gs",
            IncludesNormalize::ToLower("Stuff 3AND thIN43GS"));
}

TEST(IncludesNormalize, DifferentDrive) {
  EXPECT_EQ("stuff.h",
      IncludesNormalize::Normalize("p:\\vs08\\stuff.h", "p:\\vs08"));
  EXPECT_EQ("stuff.h",
      IncludesNormalize::Normalize("P:\\Vs08\\stuff.h", "p:\\vs08"));
  EXPECT_EQ("p:/vs08/stuff.h",
      IncludesNormalize::Normalize("p:\\vs08\\stuff.h", "c:\\vs08"));
  EXPECT_EQ("P:/vs08/stufF.h",
      IncludesNormalize::Normalize("P:\\vs08\\stufF.h", "D:\\stuff/things"));
  EXPECT_EQ("P:/vs08/stuff.h",
      IncludesNormalize::Normalize("P:/vs08\\stuff.h", "D:\\stuff/things"));
  EXPECT_EQ("P:/wee/stuff.h",
            IncludesNormalize::Normalize("P:/vs08\\../wee\\stuff.h",
                                         "D:\\stuff/things"));
}
