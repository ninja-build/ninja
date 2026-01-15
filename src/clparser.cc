// Copyright 2015 Google Inc. All Rights Reserved.
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

#include "clparser.h"

#include <algorithm>
#include <assert.h>
#include <string.h>

#include "metrics.h"
#include "string_piece.h"
#include "string_piece_util.h"

#ifdef _WIN32
#include "includes_normalize.h"
#else
#include "util.h"
#endif

using namespace std;

namespace {

/// Iterate through lines of a string (delimeted by "\r", "\n", or
/// "\r\n"), removing all lines by default unless `KeepCurrent()`
/// is called. Note that kept lines will be be changed to be
/// separated by just "\n" regardless of the original line separator.
struct InPlaceLineRemover {
  InPlaceLineRemover(std::string* str)
      : str_(str), out_(str->begin()), end_(str->size()), inStart_(0),
        inEnd_(std::min(str_->find_first_of("\r\n", inStart_), end_)),
        copyOnNext_(false) {}

  ~InPlaceLineRemover() { str_->erase(out_, str_->end()); }

  /// Return the current line.
  StringPiece Current() const {
    return StringPiece(str_->data() + inStart_, inEnd_ - inStart_);
  }

  /// Keep the current line instead of removing it from the string.
  void KeepCurrent() { copyOnNext_ = true; }

  /// Return whether we have iterated through all lines.
  bool HasNext() const { return inStart_ != end_; }

  /// Move to the next line and remove the current line from the
  /// string unless `KeepCurrent()` has been called.
  /// \pre `HasNext() == true`
  void Next() {
    assert(HasNext());

    // No need to check bounds as `str[end_] == '\0'` and we won't
    // advance further.
    std::string::size_type nextStart = inEnd_;
    nextStart = (*str_)[nextStart] == '\r' ? nextStart + 1 : nextStart;
    nextStart = (*str_)[nextStart] == '\n' ? nextStart + 1 : nextStart;

    if (copyOnNext_) {
      const StringPiece line = Current();
      out_ = std::copy(line.begin(), line.end(), out_);
      *out_++ = '\n';
      copyOnNext_ = false;
    }

    inStart_ = nextStart;
    inEnd_ = std::min(str_->find_first_of("\r\n", inStart_), end_);
  }

private:
  std::string* str_;
  std::string::iterator out_;
  std::string::size_type end_;
  std::string::size_type inStart_;
  std::string::size_type inEnd_;
  bool copyOnNext_;
};

} // anonymous namespace

// static
StringPiece CLParser::FilterShowIncludes(const StringPiece& line,
                                         const StringPiece& deps_prefix) {
  const StringPiece kDepsPrefixEnglish = "Note: including file: ";
  const char* in = line.str_;
  const char* end = in + line.len_;
  const StringPiece prefix = deps_prefix.empty() ? kDepsPrefixEnglish : deps_prefix;
  if (end - in > (int)prefix.size() &&
      memcmp(in, prefix.str_, (int)prefix.len_) == 0) {
    in += prefix.len_;
    while (*in == ' ')
      ++in;
    return line.substr(in - line.str_);
  }
  return "";
}

// static
bool CLParser::IsSystemInclude(StringPiece path, std::string* temp) {
  // TODO: Use std::search in C++17 to avoid temporaries
  temp->resize(path.size());
  transform(path.begin(), path.end(), temp->begin(), ToLowerASCII);
  // TODO: this is a heuristic, perhaps there's a better way?
  return (temp->find("program files") != string::npos ||
          temp->find("microsoft visual studio") != string::npos);
}

// static
bool CLParser::FilterInputFilename(const StringPiece& line) {
  const size_t maxFilenameSuffix = 3;
  if (line.len_ < maxFilenameSuffix + 1) {
    return false;
  }
  const auto dot = std::find(
    std::make_reverse_iterator(line.end()),
    std::make_reverse_iterator(line.end()) + maxFilenameSuffix + 1,
    '.');
  const size_t extLength = dot - std::make_reverse_iterator(line.end());
  if (extLength > maxFilenameSuffix) {
    return false;
  }
  char BUFFER[maxFilenameSuffix] = {};
  transform(line.end() - extLength, line.end(), BUFFER, ToLowerASCII);
  const StringPiece suffix(BUFFER, extLength);
  // TODO: other extensions, like .asm?
  return suffix == "c" ||
         suffix == "cc" ||
         suffix == "cxx" ||
         suffix == "cpp" ||
         suffix == "c++";
}

// static
bool CLParser::Parse(string* output, const StringPiece& deps_prefix,
                     string* err) {
  METRIC_RECORD("CLParser::Parse");

  // Loop over all lines in the output to process them.
  bool seen_show_includes = false;
#ifdef _WIN32
  IncludesNormalize normalizer(".");
#endif

  std::string normalized;
  for (InPlaceLineRemover lines(output); lines.HasNext(); lines.Next()) {
    const StringPiece line = lines.Current();
    const StringPiece include = FilterShowIncludes(line, deps_prefix);
    if (!include.empty()) {
      seen_show_includes = true;
#ifdef _WIN32
      normalized = "";
      if (!normalizer.Normalize(include, &normalized, err))
        return false;
#else
      // TODO: should this make the path relative to cwd?
      normalized.assign(include.str_, include.len_);
      uint64_t slash_bits;
      CanonicalizePath(&normalized, &slash_bits);
#endif
      if (!IsSystemInclude(normalized, &temp_))
        includes_.insert(normalized);
    } else if (!seen_show_includes && FilterInputFilename(line)) {
      // Drop it.
      // TODO: if we support compiling multiple output files in a single
      // cl.exe invocation, we should stash the filename.
    } else {
      lines.KeepCurrent();
    }
  }

  return true;
}
