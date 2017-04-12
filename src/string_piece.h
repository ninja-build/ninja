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

#ifndef NINJA_STRINGPIECE_H_
#define NINJA_STRINGPIECE_H_

#include <string>

using namespace std;

#include <string.h>

/// StringPiece represents a slice of a string whose memory is managed
/// externally.  It is useful for reducing the number of std::strings
/// we need to allocate.
struct StringPiece {
  typedef const char* const_iterator;

  StringPiece() : str_(NULL), len_(0) {}

  /// The constructors intentionally allow for implicit conversions.
  StringPiece(const string& str) : str_(str.data()), len_(str.size()) {}
  StringPiece(const char* str) : str_(str), len_(strlen(str)) {}

  StringPiece(const char* str, size_t len) : str_(str), len_(len) {}

  bool operator==(const StringPiece& other) const {
    return len_ == other.len_ && memcmp(str_, other.str_, len_) == 0;
  }
  bool operator!=(const StringPiece& other) const {
    return !(*this == other);
  }

  /// Convert the slice into a full-fledged std::string, copying the
  /// data into a new string.
  string AsString() const {
    return len_ ? string(str_, len_) : string();
  }

  const_iterator begin() const {
    return str_;
  }

  const_iterator end() const {
    return str_ + len_;
  }

  char operator[](size_t pos) const {
    return str_[pos];
  }

  size_t size() const {
    return len_;
  }

  const char* str_;
  size_t len_;
};

#endif  // NINJA_STRINGPIECE_H_
