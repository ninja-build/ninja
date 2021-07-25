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

#include "json.h"

#include <cstdio>
#include <string>

std::string EncodeJSONString(const std::string& in) {
  static const char* hex_digits = "0123456789abcdef";
  std::string out;
  out.reserve(in.length() * 1.2);
  for (std::string::const_iterator it = in.begin(); it != in.end(); ++it) {
    char c = *it;
    if (c == '\b')
      out += "\\b";
    else if (c == '\f')
      out += "\\f";
    else if (c == '\n')
      out += "\\n";
    else if (c == '\r')
      out += "\\r";
    else if (c == '\t')
      out += "\\t";
    else if (0x0 <= c && c < 0x20) {
      out += "\\u00";
      out += hex_digits[c >> 4];
      out += hex_digits[c & 0xf];
    } else if (c == '\\')
      out += "\\\\";
    else if (c == '\"')
      out += "\\\"";
    else
      out += c;
  }
  return out;
}

void PrintJSONString(const std::string& in) {
  std::string out = EncodeJSONString(in);
  fwrite(out.c_str(), 1, out.length(), stdout);
}
