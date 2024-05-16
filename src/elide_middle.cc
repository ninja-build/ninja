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
  if (str.size() <= max_width) {
    return;
  }
  // Look for an ESC character. If there is none, use a fast path
  // that avoids any intermediate allocations.
  if (str.find('\x1b') == std::string::npos) {
    const int ellipsis_width = 3;  // Space for "...".

    // If max width is too small, do not keep anything from the input.
    if (max_width <= ellipsis_width) {
      str.assign("...", max_width);
      return;
    }

    // Keep only |max_width - ellipsis_size| visible characters from the input
    // which will be split into two spans separated by "...".
    const size_t remaining_size = max_width - ellipsis_width;
    const size_t left_span_size = remaining_size / 2;
    const size_t right_span_size = remaining_size - left_span_size;

    // Replace the gap in the input between the spans with "..."
    const size_t gap_start = left_span_size;
    const size_t gap_end = str.size() - right_span_size;
    str.replace(gap_start, gap_end - gap_start, "...");
    return;
  }

  // An std::regex object to find ANSI color sequences from the input.
  const static std::regex ansi_escape("\\x1b[^m]*m");

  // Use a vector to map input string indices to a visible position
  // on the terminal line. For example:
  //
  //   |abcd|ANSI|efgh|ANSI|ijk|      input string
  //
  //                11 1111 111
  //    0123 4567 8901 2345 678       input indices
  //
  //                          1
  //    0123 4444 4567 8888 890       visible positions
  //
  //    TTTT FFFF TTTT FFFF TTT       is_visible
  struct CharInfo {
    CharInfo(size_t pos, bool visible)
        : visible_pos(pos), is_visible(visible) {}
    size_t visible_pos : 31;
    bool is_visible : 1;
  };
  std::vector<CharInfo> char_infos;
  char_infos.reserve(str.size());

  size_t visible_width = 0;
  {
    std::sregex_iterator it(str.begin(), str.end(), ansi_escape);
    std::sregex_iterator it_end;
    size_t input_index = 0;
    for (; it != it_end; ++it) {
      size_t span_start = it->position();
      size_t span_end = span_start + it->length();
      for (; input_index < span_start; ++input_index) {
        char_infos.emplace_back(visible_width++, true);
      }
      for (; input_index < span_end; ++input_index) {
        char_infos.emplace_back(visible_width, false);
      }
    }
    for (; input_index < str.size(); ++input_index)
      char_infos.emplace_back(visible_width++, true);
  }

  assert(char_infos.size() == str.size());
  assert(visible_width < str.size());

  if (visible_width <= max_width)
    return;

  // Compute the widths of the ellipsis, left span and right span
  // visible space.
  const size_t ellipsis_width = max_width < 3 ? max_width : 3;
  const size_t visible_left_span_size = (max_width - ellipsis_width) / 2;
  const size_t visible_right_span_size =
      (max_width - ellipsis_width) - visible_left_span_size;

  // Compute the gap of visible characters that will be replaced by
  // the ellipsis in visible space.
  const size_t visible_gap_start = visible_left_span_size;
  const size_t visible_gap_end = visible_width - visible_right_span_size;

  std::string result;
  result.reserve(str.size());

  // Parse the input chars info to:
  // 1) Append any characters belonging to the left span (visible or not).
  //
  // 2) Add the ellipsis ("..." truncrated to ellipsis_width).
  //    Note that its color is inherited from the left span chars
  //    which will never end with an ANSI sequence.
  //
  // 3) Append any ANSI sequence that appers inside the gap. This
  //    ensures the characters aafter the intermediate appear with
  //    the right color,
  //
  // 4) Append any remaining characters (visible or not) to the result.
  size_t input_index = 0;
  auto char_it = char_infos.begin();
  auto char_end = char_infos.end();

  // Step 1 - determine left span length in input chars.
  for (; char_it != char_end; ++char_it, input_index++) {
    if (char_it->visible_pos == visible_gap_start)
      break;
  }
  result = str.substr(0, input_index);

  // Step 2 - Append the possibly-truncated ellipsis.
  result.append("...", ellipsis_width);

  // Step 3 - Append elided ANSI sequences to the result.
  for (; char_it != char_end; ++char_it, input_index++) {
    if (char_it->visible_pos == visible_gap_end)
      break;
    if (!char_it->is_visible)
      result.push_back(str[input_index]);
  }

  // Step 4 - Append anything else.
  result.append(str.begin() + input_index, str.end());

  str = std::move(result);
}
