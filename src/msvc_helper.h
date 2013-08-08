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

string EscapeForDepfile(const string& path);

/// Visual Studio's cl.exe requires some massaging to work with Ninja;
/// for example, it emits include information on stderr in a funny
/// format when building with /showIncludes.  This class parses this
/// output.
struct CLParser {
  /// Parse a line of cl.exe output and extract /showIncludes info.
  /// If a dependency is extracted, returns a nonempty string.
  /// Exposed for testing.
  static string FilterShowIncludes(const string& line);

  /// Return true if a mentioned include file is a system path.
  /// Filtering these out reduces dependency information considerably.
  static bool IsSystemInclude(string path);

  /// Parse a line of cl.exe output and return true if it looks like
  /// it's printing an input filename.  This is a heuristic but it appears
  /// to be the best we can do.
  /// Exposed for testing.
  static bool FilterInputFilename(string line);

  /// Parse the full output of cl, returning the output (if any) that
  /// should printed.
  string Parse(const string& output);

  set<string> includes_;
};

/// Wraps a synchronous execution of a CL subprocess.
struct CLWrapper {
  CLWrapper() : env_block_(NULL) {}

  /// Set the environment block (as suitable for CreateProcess) to be used
  /// by Run().
  void SetEnvBlock(void* env_block) { env_block_ = env_block; }

  /// Start a process and gather its raw output.  Returns its exit code.
  /// Crashes (calls Fatal()) on error.
  int Run(const string& command, string* output);

  void* env_block_;
};
