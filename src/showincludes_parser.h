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

/// Visual Studio's cl.exe emits include information on stderr
/// when building with /showIncludes.  This module parses that output
/// to extract the file list.

#include <string>
#include <vector>
using namespace std;

struct StringPiece;

struct ShowIncludes {
  /// Parse the cl.exe output to stderr, extract the file list, and
  /// return the filtered output (which may contain e.g. warning
  /// information).
  static string Filter(const string& output,
                       vector<StringPiece>* includes);
};
