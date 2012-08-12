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

#include "msvc_helper.h"

#include <string.h>

#include "string_piece.h"

// static
string CLWrapper::FilterShowIncludes(const string& line) {
  static const char kMagicPrefix[] = "Note: including file: ";
  const char* in = line.c_str();
  const char* end = in + line.size();

  if (end - in > (int)sizeof(kMagicPrefix) - 1 &&
      memcmp(in, kMagicPrefix, sizeof(kMagicPrefix) - 1) == 0) {
    in += sizeof(kMagicPrefix) - 1;
    while (*in == ' ')
      ++in;
    return line.substr(in - line.c_str());
  }
  return "";
}
