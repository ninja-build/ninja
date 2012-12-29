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
#include <set>
#include <vector>
using namespace std;

/// Visual Studio's cl.exe requires some massaging to work with Ninja;
/// for example, it emits include information on stderr in a funny
/// format when building with /showIncludes.  This class wraps a CL
/// process and parses that output to extract the file list.
struct CLWrapper {
  CLWrapper() : env_block_(NULL) {}

  /// Set the environment block (as suitable for CreateProcess) to be used
  /// by Run().
  void SetEnvBlock(void* env_block) { env_block_ = env_block; }

  /// Start a process and parse its output.  Returns its exit code.
  /// Any non-parsed output is buffered into \a extra_output if provided,
  /// otherwise it is printed to stdout while the process runs.
  /// Crashes (calls Fatal()) on error.
  int Run(const string& command, string* extra_output=NULL);

  /// Parse a line of cl.exe output and extract /showIncludes info.
  /// If a dependency is extracted, returns a nonempty string.
  /// Exposed for testing.
  static string FilterShowIncludes(const string& line);

  /// Return true if a mentioned include file is a system path.
  /// Expects the path to already by normalized (including lower case).
  /// Filtering these out reduces dependency information considerably.
  static bool IsSystemInclude(const string& path);

  /// Parse a line of cl.exe output and return true if it looks like
  /// it's printing an input filename.  This is a heuristic but it appears
  /// to be the best we can do.
  /// Exposed for testing.
  static bool FilterInputFilename(const string& line);

  /// Fill a vector with the unique'd headers, escaped for output as a .d
  /// file.
  vector<string> GetEscapedResult();

  void* env_block_;
  set<string> includes_;
};
