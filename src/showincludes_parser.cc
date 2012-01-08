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

#include "showincludes_parser.h"

#include <string.h>

#include "string_piece.h"

// static
string ShowIncludes::Filter(const string& output,
                            vector<StringPiece>* includes) {
  string filtered;
  static const char kMagicPrefix[] = "Note: including file: ";
  const char* in = output.c_str();
  const char* end = in + output.size();
  while (in < end) {
    const char* next = strchr(in, '\n');
    if (next)
      ++next;
    else
      next = end;

    if (end - in > (int)sizeof(kMagicPrefix) - 1 &&
        memcmp(in, kMagicPrefix, sizeof(kMagicPrefix) - 1) == 0) {
      in += sizeof(kMagicPrefix) - 1;
      while (*in == ' ')
        ++in;
      int len = next - in;
      while (len > 0 && (in[len - 1] == '\n' || in[len - 1] == '\r'))
        --len;
      includes->push_back(StringPiece(in, len));
    } else {
      filtered.append(string(in, next - in));
    }

    in = next;
  }
  return filtered;
}
