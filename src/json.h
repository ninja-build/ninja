// Copyright 2021 Google Inc. All Rights Reserved.
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

#ifndef NINJA_JSON_H_
#define NINJA_JSON_H_

#include <stdio.h>

void EncodeJSONString(FILE* stream, const char* str) {
  while (*str) {
    char c = *str;
    if (c == '\b')
      fputs("\\b", stream);
    else if (c == '\f')
      fputs("\\f", stream);
    else if (c == '\n')
      fputs("\\n", stream);
    else if (c == '\r')
      fputs("\\r", stream);
    else if (c == '\t')
      fputs("\\t", stream);
    else if (0x0 <= c && c < 0x20)
      fprintf(stream, "\\u%04x", *str);
    else if (c == '\\')
      fputs("\\\\", stream);
    else if (c == '\"')
      fputs("\\\"", stream);
    else
      fputc(c, stream);
    str++;
  }
}

#endif
