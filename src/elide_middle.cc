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

#include "elide_middle.h"

#include <assert.h>
#include <string.h>

#include <regex>

void ElideMiddleInPlace(std::string& str, size_t max_width) {
  if (max_width <= 3) {
    str.assign("...", max_width);
    return;
  }
  const int kMargin = 3;  // Space for "...".
  const static std::regex ansi_escape("\\x1b[^m]*m");
  std::string result = std::regex_replace(str, ansi_escape, "");
  if (result.size() <= max_width)
    return;

  int32_t elide_size = (max_width - kMargin) / 2;

  std::vector<std::pair<int32_t, std::string>> escapes;
  size_t added_len = 0;  // total number of characters

  std::sregex_iterator it(str.begin(), str.end(), ansi_escape);
  std::sregex_iterator end;
  while (it != end) {
    escapes.emplace_back(it->position() - added_len, it->str());
    added_len += it->str().size();
    ++it;
  }

  std::string new_status =
      result.substr(0, elide_size) + "..." +
      result.substr(result.size() - elide_size - ((max_width - kMargin) % 2));

  added_len = 0;
  // We need to put all ANSI escape codes back in:
  for (const auto& escape : escapes) {
    int32_t pos = escape.first;
    if (pos > elide_size) {
      pos -= result.size() - max_width;
      if (pos < static_cast<int32_t>(max_width) - elide_size) {
        pos = max_width - elide_size - (max_width % 2 == 0 ? 1 : 0);
      }
    }
    pos += added_len;
    new_status.insert(pos, escape.second);
    added_len += escape.second.size();
  }
  str = std::move(new_status);
}
