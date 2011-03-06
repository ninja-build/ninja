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

#include "test.h"

#include "parsers.h"

StateTestWithBuiltinRules::StateTestWithBuiltinRules() {
  AssertParse(&state_,
"rule cat\n"
"  command = cat $in > $out\n");
}

Node* StateTestWithBuiltinRules::GetNode(const string& path) {
  return state_.GetNode(path);
}

void AssertParse(State* state, const char* input) {
  ManifestParser parser(state, NULL);
  string err;
  ASSERT_TRUE(parser.Parse(input, &err)) << err;
  ASSERT_EQ("", err);
}

void VirtualFileSystem::Create(const string& path, int time,
                               const string& contents) {
  files_[path].mtime = time;
  files_[path].contents = contents;
}

int VirtualFileSystem::Stat(const string& path) {
  FileMap::iterator i = files_.find(path);
  if (i != files_.end())
    return i->second.mtime;
  return 0;
}

bool VirtualFileSystem::MakeDir(const string& path) {
  directories_made_.push_back(path);
  return true;  // success
}

string VirtualFileSystem::ReadFile(const string& path, string* err) {
  files_read_.push_back(path);
  FileMap::iterator i = files_.find(path);
  if (i != files_.end())
    return i->second.contents;
  return "";
}
