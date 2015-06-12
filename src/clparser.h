// Copyright 2015 Google Inc. All Rights Reserved.
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

#ifndef NINJA_CLPARSER_H_
#define NINJA_CLPARSER_H_

#include <set>
#include <string>
using namespace std;

/// Visual Studio's cl.exe requires some massaging to work with Ninja;
/// for example, it emits include information on stderr in a funny
/// format when building with /showIncludes.  This class parses this
/// output.
struct CLParser {
  /// Parse a line of cl.exe output and extract /showIncludes info.
  /// If a dependency is extracted, returns a nonempty string.
  /// Exposed for testing.
  static string FilterShowIncludes(const string& line,
                                   const string& deps_prefix);

  /// Return true if a mentioned include file is a system path.
  /// Filtering these out reduces dependency information considerably.
  static bool IsSystemInclude(string path);

  /// Parse a line of cl.exe output and return true if it looks like
  /// it's printing an input filename.  This is a heuristic but it appears
  /// to be the best we can do.
  /// Exposed for testing.
  static bool FilterInputFilename(string line);

  /// Parse the full output of cl, filling filtered_output with the text that
  /// should be printed (if any). Returns true on success, or false with err
  /// filled. output must not be the same object as filtered_object.
  bool Parse(const string& output, const string& deps_prefix,
             string* filtered_output, string* err);

  set<string> includes_;
};

#endif  // NINJA_CLPARSER_H_
