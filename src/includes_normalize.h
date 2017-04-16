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

#include <string>
#include <vector>
using namespace std;

struct StringPiece;

/// Utility functions for normalizing include paths on Windows.
/// TODO: this likely duplicates functionality of CanonicalizePath; refactor.
namespace IncludesNormalize {
  // Internal utilities made available for testing, maybe useful otherwise.
  string Join(const vector<string>& list, char sep);
  vector<string> Split(const string& input, char sep);
  string ToLower(const string& s);
  string AbsPath(StringPiece s);
  string Relativize(StringPiece path, const string& start);

  /// Normalize by fixing slashes style, fixing redundant .. and . and makes the
  /// path relative to |relative_to|.
  bool Normalize(const string& input, const char* relative_to, string* result,
                 string* err);
};
