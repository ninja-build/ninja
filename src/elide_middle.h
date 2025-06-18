// Copyright 2024 Google Inc. All Rights Reserved.
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

#ifndef NINJA_ELIDE_MIDDLE_H_
#define NINJA_ELIDE_MIDDLE_H_

#include <cstddef>
#include <string>

/// Elide the given string @a str with '...' in the middle if the length
/// exceeds @a max_width. Note that this handles ANSI color sequences
/// properly (non-color related sequences are ignored, but using them
/// would wreak the cursor position or terminal state anyway).
void ElideMiddleInPlace(std::string& str, size_t max_width);

#endif  // NINJA_ELIDE_MIDDLE_H_
