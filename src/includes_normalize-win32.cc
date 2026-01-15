// Copyright 2012 Google Inc. All Rights Reserved.
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

#include "includes_normalize.h"

#include "string_piece.h"
#include "string_piece_util.h"
#include "util.h"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <sstream>

#include <windows.h>

using namespace std;

namespace {

bool InternalGetFullPathName(const char *file_name, char* buffer,
                             size_t buffer_length, string *err) {
  DWORD result_size = GetFullPathNameA(file_name,
                                       buffer_length, buffer, NULL);
  if (result_size == 0) {
    *err = "GetFullPathNameA(";
    *err += file_name;
    *err += "): ";
    *err += GetLastErrorString();
    return false;
  } else if (result_size > buffer_length) {
    *err = "path too long";
    return false;
  }
  return true;
}

bool IsPathSeparator(char c) {
  return c == '/' ||  c == '\\';
}

// Return true if paths a and b are on the same windows drive.
// Return false if this function cannot check
// whether or not on the same windows drive.
bool SameDriveFast(const StringPiece& a, const StringPiece& b) {
  if (a.size() < 3 || b.size() < 3) {
    return false;
  }

  if (!islatinalpha(a[0]) || !islatinalpha(b[0])) {
    return false;
  }

  if (ToLowerASCII(a[0]) != ToLowerASCII(b[0])) {
    return false;
  }

  if (a[1] != ':' || b[1] != ':') {
    return false;
  }

  return IsPathSeparator(a[2]) && IsPathSeparator(b[2]);
}

// Return true if paths a and b are on the same Windows drive.
bool SameDrive(const std::string& a, const std::string& b, string* err)  {
  if (SameDriveFast(a, b)) {
    return true;
  }

  char a_absolute[_MAX_PATH];
  char b_absolute[_MAX_PATH];
  if (!InternalGetFullPathName(a.c_str(), a_absolute, sizeof(a_absolute), err)) {
    return false;
  }
  if (!InternalGetFullPathName(b.c_str(), b_absolute, sizeof(b_absolute),
                               err)) {
    return false;
  }
  char a_drive[_MAX_DIR];
  char b_drive[_MAX_DIR];
  _splitpath(a_absolute, a_drive, NULL, NULL, NULL);
  _splitpath(b_absolute, b_drive, NULL, NULL, NULL);
  return _stricmp(a_drive, b_drive) == 0;
}

// Check path |s| is FullPath style returned by GetFullPathName.
// This ignores difference of path separator.
// This is used not to call very slow GetFullPathName API.
bool IsFullPathName(StringPiece s) {
  if (s.size() < 3 ||
      !islatinalpha(s[0]) ||
      s[1] != ':' ||
      !IsPathSeparator(s[2])) {
    return false;
  }

  // Check "." or ".." is contained in path.
  for (size_t i = 2; i < s.size(); ++i) {
    if (!IsPathSeparator(s[i])) {
      continue;
    }

    // Check ".".
    if (i + 1 < s.size() && s[i+1] == '.' &&
        (i + 2 >= s.size() || IsPathSeparator(s[i+2]))) {
      return false;
    }

    // Check "..".
    if (i + 2 < s.size() && s[i+1] == '.' && s[i+2] == '.' &&
        (i + 3 >= s.size() || IsPathSeparator(s[i+3]))) {
      return false;
    }
  }

  return true;
}

#ifdef NDEBUG
void AssertIsAbsoluteInDebug(const StringPiece&) {
#else
void AssertIsAbsoluteInDebug(const StringPiece& s) {
  std::string err;
  std::string copy = s.AsString();
  IncludesNormalize::AbsPath(&copy, &err);
  assert(StringPiece(copy) == s);
#endif
}

struct StringPieceRange {
  struct const_iterator {
    using iterator_category = std::forward_iterator_tag;
    using value_type        = StringPiece;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const StringPiece*;
    using reference         = const StringPiece&;

    const_iterator(const StringPiece& str, const char *end, char sep)
        : str_(str), end_(end), sep_(sep) {}

    const_iterator& operator++() {
      if (str_.end() == end_) {
        str_ = StringPiece(str_.end(), 0);
      } else {
        const auto start = str_.end() + 1;
        const auto sep_it = std::find(start, end_, sep_);
        str_ = StringPiece(start, sep_it - start);
      }
      return *this;
    }

    reference operator*() const {
      return str_;
    }

    pointer operator->() const { return &str_; }

    friend bool operator==(
        const const_iterator& lhs, const const_iterator& rhs) {
      return lhs.str_.str_ == rhs.str_.str_;
    }

    friend bool operator!=(
        const const_iterator& lhs, const const_iterator& rhs) {
      return !(lhs == rhs);
    }

    StringPiece str_;
    const char* end_;
    char sep_;
  };

  StringPieceRange(const StringPiece& str, char sep)
    : str_(str), sep_(sep) {}

  const_iterator begin() const {
    const auto it = std::find(str_.begin(), str_.end(), sep_);
    return const_iterator(StringPiece(str_.begin(), it - str_.begin()),
                          str_.end(), sep_);
  }

  const_iterator end() const {
    return const_iterator(StringPiece(str_.end(), 0), str_.end(), sep_);
  }

  StringPiece str_;
  char sep_;
};

}  // anonymous namespace

IncludesNormalize::IncludesNormalize(const StringPiece& relative_to)
 : relative_to_(relative_to.str_, relative_to.len_) {
  string err;
  AbsPath(&relative_to_, &err);
  if (!err.empty()) {
    Fatal("Initializing IncludesNormalize(): %s", err.c_str());
  }
  split_relative_to_ = SplitStringPiece(relative_to_, '/');
}

void IncludesNormalize::AbsPath(std::string *s, string* err) {
  if (IsFullPathName(*s)) {
    for (char& ch : *s) {
      if (ch == '\\') {
        ch = '/';
      }
    }
    return;
  }

  char result[_MAX_PATH];
  if (!InternalGetFullPathName(s->c_str(), result, sizeof(result), err)) {
    s->clear();
    return;
  }
  char* c = result;
  for (; *c; ++c)
    if (*c == '\\')
      *c = '/';
  s->assign(result, c);
}

void IncludesNormalize::Relativize(
    std::string *abs_path, const vector<StringPiece>& start_list, string* err) {
  AssertIsAbsoluteInDebug(*abs_path);

  const StringPieceRange path_list(*abs_path, '/');
  const auto diff =
      std::mismatch(path_list.begin(), path_list.end(), start_list.begin(),
                    start_list.end(), EqualsCaseInsensitiveASCII);
  const size_t sections_to_replace = start_list.end() - diff.second;
  const StringPiece dotdot = "../";
  const size_t bytes_to_write = sections_to_replace * dotdot.size();
  const size_t bytes_to_delete = diff.first->str_ - abs_path->data();
  if (bytes_to_write > bytes_to_delete) {
    // We can insert chars at any position from
    // [begin(), begin() + bytes_to_delete) so we choose the last
    // element to minimize copying
    abs_path->insert(abs_path->begin() + bytes_to_delete,
                     bytes_to_write - bytes_to_delete, '\0');
  } else if (bytes_to_write < bytes_to_delete) {
    abs_path->erase(abs_path->begin(),
                    abs_path->begin() + bytes_to_delete - bytes_to_write);
  }
  auto it = abs_path->begin();
  for (size_t i = 0; i < sections_to_replace; ++i) {
    it = std::copy(dotdot.begin(), dotdot.end(), it);
  }

  if (abs_path->empty()) {
    *abs_path = '.';
  }
}

bool IncludesNormalize::Normalize(const StringPiece& input,
                                  string* result, string* err) const {
  char copy[_MAX_PATH + 1];
  size_t len = input.size();
  if (len > _MAX_PATH) {
    *err = "path too long";
    return false;
  }
  strncpy(copy, input.str_, input.len_);
  copy[len] = '\0';
  uint64_t slash_bits;
  CanonicalizePath(copy, &len, &slash_bits);
  result->assign(copy, len);
  AbsPath(result, err);
  if (!err->empty())
    return false;

  if (!SameDrive(*result, relative_to_, err)) {
    if (!err->empty())
      return false;
    result->assign(copy, len);
    return true;
  }
  Relativize(result, split_relative_to_, err);
  if (!err->empty())
    return false;
  return true;
}
