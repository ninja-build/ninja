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

#ifndef NINJA_DEPFILE_PARSER_H_
#define NINJA_DEPFILE_PARSER_H_

#include <string>
#include <set>
#include <vector>
using namespace std;

#include "string_piece.h"

/// Parser for the dependency information emitted by gcc's -M flags.
struct DepfileParser {
  /// Parse an input file.  Input must be NUL-terminated.
  /// Warning: may mutate the content in-place.
  /// current_deps contains the set of Canonicalized paths which are
  ///   explicit+implicit dependencies. DepfileParser adds all new
  ///   implicit dependencies to it during parsing.
  bool Parse(string* content, string* err, set<string>* current_deps);

  StringPiece out_;
  vector<string> ins_;
};

#endif // NINJA_DEPFILE_PARSER_H_
