// Copyright 2025 Google Inc. All Rights Reserved.
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

#ifndef NINJA_CLSOURCEDEPENDENCIESPARSER_H_
#define NINJA_CLSOURCEDEPENDENCIESPARSER_H_

#include <string>
#include <vector>

#include "string_piece.h"

/// Parse a JSON file produced by "cl /sourceDependencies"
/// and append the included paths to the includes vector.
/// Returns true on success, or false with *err filled.
///
/// Note: these will be lower-case paths even if Ninja
///       passed mixed-case paths to the compiler.
bool ParseCLSourceDependencies(StringPiece content,
                               std::vector<std::string>* includes,
                               std::string* err);

#endif  // NINJA_CLSOURCEDEPENDENCIESPARSER_H_
