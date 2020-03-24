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

struct DepfileParserOptions {
  DepfileParserOptions() {}
};

/// Parser for the dependency information emitted by gcc's -M flags.
struct DepfileParser {
  explicit DepfileParser(DepfileParserOptions options =
                         DepfileParserOptions());

  /// Parse an input file.  Input must be NUL-terminated.
  /// Warning: may mutate the content in-place and parsed StringPieces are
  /// pointers within it.
  bool Parse(string* content, string* err);

  std::vector<StringPiece> outs_;
  vector<StringPiece> ins_;
  DepfileParserOptions options_;
};

#endif // NINJA_DEPFILE_PARSER_H_
