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

#ifndef NINJA_EVALSTRING_H_
#define NINJA_EVALSTRING_H_

#include <string>
#include <utility>

class EvalStringBuilder;
struct StringPiece;

/// A tokenized string that contains variable references.
/// Can be evaluated relative to an Env.
class EvalString {
public:
  using Offset = std::size_t;

  friend class EvalStringBuilder;

  enum class TokenType {
    RAW = 0,     ///< Raw text
    SPECIAL = 1, ///< A variable
  };

  class const_iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = std::pair<StringPiece, TokenType>;
    using pointer = const value_type*;
    using reference = const value_type&;

    const_iterator();
    const_iterator(const char* pos);
    std::pair<StringPiece, TokenType> operator*() const;
    const_iterator& operator++();
    const_iterator operator++(int);
    friend bool operator==(const_iterator lhs, const_iterator rhs);
    friend bool operator!=(const_iterator lhs, const_iterator rhs);
  private:
    const char* pos_;
  };

  /// Create an empty EvalString with no tokens.
  EvalString();

  /// Return whether this object has no tokens.
  bool empty() const;

  /// Return an iterator to the first token.
  const_iterator begin() const;

  /// Return an iterator to one past the last token.
  const_iterator end() const;

  /// @return The string with variables not expanded.
  std::string Unparse() const;

  /// Construct a human-readable representation of the parsed state
  /// for use in tests.
  std::string Serialize() const;

private:
  std::string data_;
};

/// A class to create EvalString objects.
class EvalStringBuilder {
public:
  /// Create an EvalStringBuilder with no tokens.
  EvalStringBuilder();

  /// Return whether the held EvalString has any tokens.
  bool empty() const;

  /// Clear the held EvalString.
  void Clear();

  /// Append (or extend if the last token is already raw text) a raw text
  /// token to the end of the held EvalString.
  /// @pre `!text.empty()`
  void AddText(StringPiece text);

  /// Append a special variable token to the end of the held EvalString.
  /// @pre `!text.empty()`.
  void AddSpecial(StringPiece text);

  /// Return a const reference to the held EvalString.
  const EvalString& str() const &;
  operator const EvalString&() const;

  /// Return a copy of the EvalString by move-constructing the held string.
  /// @post After calling this method `this->empty() == true`.
  EvalString str() &&;

private:
  EvalString str_;
  EvalString::Offset lastTextSegmentLength_ = 0;
};

#endif  // NINJA_EVALSTRING_H_
