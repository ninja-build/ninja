// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License")
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

#if NINJA_ENABLE_CL_SOURCE_DEPENDENCIES

#ifndef NINJA_CLSOURCEDEPENDENCIESPARSER_H_
#define NINJA_CLSOURCEDEPENDENCIESPARSER_H_


#include <set>
#include <string>

#include "string_piece.h"

/// Parse the JSON file produced by /sourceDependencies, filling includes with
/// the includes. Returns true on success, or false with err filled.
bool ParseCLSourceDependencies(const StringPiece content, std::string* err,
                               std::set<std::string>& includes);

#endif  // NINJA_CLSOURCEDEPENDENCIESPARSER_H_
#endif  // NINJA_ENABLE_CL_SOURCE_DEPENDENCIES
