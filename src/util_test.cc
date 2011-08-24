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

#include "util.h"

#include "test.h"

TEST(CanonicalizePath, PathSamples) {
  std::string path = "foo.h";
  CanonicalizePath(&path);
  EXPECT_EQ("foo.h", path);

  path = "./foo.h";
  CanonicalizePath(&path);
  EXPECT_EQ("foo.h", path);

  path = "./foo/./bar.h";
  CanonicalizePath(&path);
  EXPECT_EQ("foo/bar.h", path);

  path = "./x/foo/../bar.h";
  CanonicalizePath(&path);
  EXPECT_EQ("x/bar.h", path);

  path = "./x/foo/../../bar.h";
  CanonicalizePath(&path);
  EXPECT_EQ("bar.h", path);

  path = "foo//bar";
  CanonicalizePath(&path);
  EXPECT_EQ("foo/bar", path);

  path = "foo//.//..///bar";
  CanonicalizePath(&path);
  EXPECT_EQ("bar", path);

  path = "./x/../foo/../../bar.h";
  CanonicalizePath(&path);
  EXPECT_EQ("../bar.h", path);
}

TEST(CanonicalizePath, UpDir) {
  std::string path, err;
  path = "../../foo/bar.h";
  CanonicalizePath(&path);
  EXPECT_EQ("../../foo/bar.h", path);

  path = "test/../../foo/bar.h";
  CanonicalizePath(&path);
  EXPECT_EQ("../foo/bar.h", path);
}

TEST(CanonicalizePath, AbsolutePath) {
  string path = "/usr/include/stdio.h";
  CanonicalizePath(&path);
  EXPECT_EQ("/usr/include/stdio.h", path);
}
