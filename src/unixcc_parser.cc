// Copyright 2016 SAP SE All Rights Reserved.
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

#include "unixcc_parser.h"
#include <cstring>

// Characters which we consider to be valid in a filename.
static const char filename_characters[] = 
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    "-_/.";

void UnixCCParser::Parse(const string& output, string* filtered_output) {
  const char* in = &output[0];
  const char* end = in + output.size();

  while (in < end) {
    const char* const line_start = in;

    // Advance past initial tabs.
    while (in < end && *in == '\t')
      ++in;

    const char* const filename_start = in;

    // Treat only a small set of allowed characters as part of a filename.
    while (in < end && strchr(filename_characters, *in) != NULL)
      ++in;

    const char* const filename_end = in;

    // Only allow a filename if it is non-empty and immediately followed by a
    // newline.
    if (filename_start + 1 < in && in < end && *in == '\n') {
      ++in;
      string filename(filename_start, filename_end);
      includes_.insert(filename);
      continue;
    }

    // Forward to the end of the current line.
    while (in < end && *in != '\n')
      ++in;

    if (in < end)
        ++in;

    filtered_output->append(line_start, in);
  }
}
