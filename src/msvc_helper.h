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

#include <string>
#include <vector>
using namespace std;

struct StringPiece;

/// Visual Studio's cl.exe requires some massaging to work with Ninja;
/// for example, it emits include information on stderr in a funny
/// format when building with /showIncludes.  This class wraps a CL
/// process and parses that output to extract the file list.
struct CLWrapper {
  /// Start a process and parse its output.  Returns its exit code.
  /// Any non-parsed output is buffered into \a extra_output if provided,
  /// otherwise it is printed to stdout while the process runs.
  /// Crashes (calls Fatal()) on error.
  int Run(const string& command, string* extra_output=NULL);

  /// Parse a line of cl.exe output and extract /showIncludes info.
  /// If a dependency is extracted, returns a nonempty string.
  /// Exposed for testing.
  static string FilterShowIncludes(const string& line);

  vector<string> includes_;
};
