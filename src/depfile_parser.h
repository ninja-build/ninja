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
#include <vector>
using namespace std;

#include "string_piece.h"

/// Parser for the dependency information emitted by gcc's -M flags.
struct DepfileParser {
  /// Parse an input file with GCC-formatted deps.  Input must be
  /// NUL-terminated.
  /// Warning: may mutate the content in-place and parsed StringPieces are
  /// pointers within it.
  bool ParseGcc(string* content, string* err);

  /// Parses the "list" format of a dep file, where the format is one-file-per
  /// line with no other transformations applied. Can not fail.
  /// Warning: parsed StringPieces are pointers to within the content.
  void ParseList(const string& content);

  StringPiece out_;
  vector<StringPiece> ins_;
};

/// Parser for the "list" format for depfiles (file-per-line).
struct DepfileListParser {
  /// Parse an input file. This format is very simple so there are no errors
  /// that can be generated.
  /// Warning: parsed StringPieces are pointers within the content string.
  void Parse(const string& content);

  vector<StringPiece> ins_;
};

#endif // NINJA_DEPFILE_PARSER_H_
