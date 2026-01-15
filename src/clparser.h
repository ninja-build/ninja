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

struct StringPiece;

/// Visual Studio's cl.exe requires some massaging to work with Ninja;
/// for example, it emits include information on stderr in a funny
/// format when building with /showIncludes.  This class parses this
/// output.
struct CLParser {
  /// Parse a line of cl.exe output and extract /showIncludes info.
  /// If a dependency is extracted, returns a nonempty string.
  /// Exposed for testing.
  static StringPiece FilterShowIncludes(const StringPiece& line,
                                        const StringPiece& deps_prefix);

  /// Return true if a mentioned include file is a system path.
  /// Use \@ temp to store temporary data if needed.
  /// Filtering these out reduces dependency information considerably.
  static bool IsSystemInclude(StringPiece path, std::string* temp);

  /// Parse a line of cl.exe output and return true if it looks like
  /// it's printing an input filename.  This is a heuristic but it appears
  /// to be the best we can do.
  /// Exposed for testing.
  static bool FilterInputFilename(const StringPiece& line);

  /// Parse the full output of cl, updating /a output with the text that
  /// should be printed (if any). Returns true on success, or false with err
  /// filled.
  bool Parse(std::string *output, const StringPiece& deps_prefix,
             std::string* err);

  std::set<std::string> includes_;
  std::string temp_;
};

#endif  // NINJA_CLPARSER_H_
