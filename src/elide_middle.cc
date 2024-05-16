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

// Convenience class used to iterate over the ANSI color sequences
// of an input string. Note that this ignores non-color related
// ANSI sequences. Usage is:
//
//  - Create instance, passing the input string to the constructor.
//  - Loop over each sequence with:
//
//        AnsiColorSequenceIterator iter;
//        while (iter.HasSequence()) {
//          .. use iter.SequenceStart() and iter.SequenceEnd()
//          iter.NextSequence();
//        }
//
struct AnsiColorSequenceIterator {
  // Constructor takes input string .
  AnsiColorSequenceIterator(const std::string& input)
      : input_(input.data()), input_end_(input_ + input.size()) {
    FindNextSequenceFrom(input_);
  }

  // Return true if an ANSI sequence was found.
  bool HasSequence() const { return cur_end_ != 0; }

  // Start of the current sequence.
  size_t SequenceStart() const { return cur_start_; }

  // End of the current sequence (index of the first character
  // following the sequence).
  size_t SequenceEnd() const { return cur_end_; }

  // Size of the current sequence in characters.
  size_t SequenceSize() const { return cur_end_ - cur_start_; }

  // Returns true if |input_index| belongs to the current sequence.
  bool SequenceContains(size_t input_index) const {
    return (input_index >= cur_start_ && input_index < cur_end_);
  }

  // Find the next sequence, if any, from the input.
  // Returns false is there is no more sequence.
  bool NextSequence() {
    if (FindNextSequenceFrom(input_ + cur_end_))
      return true;

    cur_start_ = 0;
    cur_end_ = 0;
    return false;
  }

  // Reset iterator to start of input.
  void Reset() {
    cur_start_ = cur_end_ = 0;
    FindNextSequenceFrom(input_);
  }

 private:
  // Find the next sequence from the input, |from| being the starting position
  // for the search, and must be in the [input_, input_end_] interval. On
  // success, returns true after setting cur_start_ and cur_end_, on failure,
  // return false.
  bool FindNextSequenceFrom(const char* from) {
    assert(from >= input_ && from <= input_end_);
    auto* seq =
        static_cast<const char*>(::memchr(from, '\x1b', input_end_ - from));
    if (!seq)
      return false;

    // The smallest possible color sequence if '\x1c[0m` and has four
    // characters.
    if (seq + 4 > input_end_)
      return false;

    if (seq[1] != '[')
      return FindNextSequenceFrom(seq + 1);

    // Skip parameters (digits + ; separator)
    auto is_parameter_char = [](char ch) -> bool {
      return (ch >= '0' && ch <= '9') || ch == ';';
    };

    const char* end = seq + 2;
    while (is_parameter_char(end[0])) {
      if (++end == input_end_)
        return false;  // Incomplete sequence (no command).
    }

    if (*end++ != 'm') {
      // Not a color sequence. Restart the search after the first
      // character following the [, in case this was a 3-char ANSI
      // sequence (which is ignored here).
      return FindNextSequenceFrom(seq + 3);
    }

    // Found it!
    cur_start_ = seq - input_;
    cur_end_ = end - input_;
    return true;
  }

  size_t cur_start_ = 0;
  size_t cur_end_ = 0;
  const char* input_;
  const char* input_end_;
};

// A class used to iterate over all characters of an input string,
// and return its visible position in the terminal, and whether that
// specific character is visible (or otherwise part of an ANSI color sequence).
//
// Example sequence and iterations, where 'ANSI' represents an ANSI Color
// sequence, and | is used to express concatenation
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
//
// Usage is:
//
//     VisibleInputCharsIterator iter(input);
//     while (iter.HasChar()) {
//       ... use iter.InputIndex() to get input index of current char.
//       ... use iter.VisiblePosition() to get its visible position.
//       ... use iter.IsVisible() to check whether the current char is visible.
//
//       NextChar();
//     }
//
struct VisibleInputCharsIterator {
  VisibleInputCharsIterator(const std::string& input)
      : input_size_(input.size()), ansi_iter_(input) {}

  // Return true if there is a character in the sequence.
  bool HasChar() const { return input_index_ < input_size_; }

  // Return current input index.
  size_t InputIndex() const { return input_index_; }

  // Return current visible position.
  size_t VisiblePosition() const { return visible_pos_; }

  // Return true if the current input character is visible
  // (i.e. not part of an ANSI color sequence).
  bool IsVisible() const { return !ansi_iter_.SequenceContains(input_index_); }

  // Find next character from the input.
  void NextChar() {
    visible_pos_ += IsVisible();
    if (++input_index_ == ansi_iter_.SequenceEnd()) {
      ansi_iter_.NextSequence();
    }
  }

 private:
  size_t input_size_;
  size_t input_index_ = 0;
  size_t visible_pos_ = 0;
  AnsiColorSequenceIterator ansi_iter_;
};

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

  // Compute visible width.
  size_t visible_width = str.size();
  for (AnsiColorSequenceIterator ansi(str); ansi.HasSequence();
       ansi.NextSequence()) {
    visible_width -= ansi.SequenceSize();
  }

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
  //
  // 1) Append any characters belonging to the left span (visible or not).
  //
  // 2) Add the ellipsis ("..." truncated to ellipsis_width).
  //    Note that its color is inherited from the left span chars
  //    which will never end with an ANSI sequence.
  //
  // 3) Append any ANSI sequence that appears inside the gap. This
  //    ensures the characters after the ellipsis appear with
  //    the right color,
  //
  // 4) Append any remaining characters (visible or not) to the result.
  //
  VisibleInputCharsIterator iter(str);

  // Step 1 - determine left span length in input chars.
  for (; iter.HasChar(); iter.NextChar()) {
    if (iter.VisiblePosition() == visible_gap_start)
      break;
  }
  result.append(str.begin(), str.begin() + iter.InputIndex());

  // Step 2 - Append the possibly-truncated ellipsis.
  result.append("...", ellipsis_width);

  // Step 3 - Append elided ANSI sequences to the result.
  for (; iter.HasChar(); iter.NextChar()) {
    if (iter.VisiblePosition() == visible_gap_end)
      break;
    if (!iter.IsVisible())
      result.push_back(str[iter.InputIndex()]);
  }

  // Step 4 - Append anything else.
  result.append(str.begin() + iter.InputIndex(), str.end());

  str = std::move(result);
}
