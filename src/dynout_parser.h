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

#ifndef NINJA_DYNOUT_PARSER_H_
#define NINJA_DYNOUT_PARSER_H_

#include <string>
#include <vector>

#include "disk_interface.h"
#include "state.h"

/// Parser for dynout file.
struct DynoutParser {

  /// Parse an dynout file.
  /// Add  Input must be NUL-terminated.
  /// Warning: may mutate the content in-place and parsed StringPieces are
  /// pointers within it.
  static bool Parse(State* state, DiskInterface* disk_interface,
             Edge* edge, const std::string& path, 
             std::vector<Node*>* nodes, int* outputs_count,
             std::string* err);
};

#endif // NINJA_DYNOUT_PARSER_H_
