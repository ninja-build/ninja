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

#include "evalstring.h"

#include "string_piece.h"

#include <cassert>
#include <algorithm>
#include <limits>

// The format of EvalString is a sequence of tokens. Each segment is
// prefixed with the length of the token, stored as an Offset type.  The
// leading bit of this is set to 1 if the token is a variable, otherwise it is
// 0 if the token is text.  Additionally, when building with
// EvalStringBuilder there is an extra member variable
// lastTextSegmentLength_ that has the length of the last text section or 0
// if the last section was not text. This allows us to jump back to the last
// token to extend it.
//
// This has the benefit that EvalString if very cache-friendly when iterating
// and requires only one allocation.  Moves and copies should be as cheap as
// possible as well.
//
// The final benefit is that when we call Clear() we don't free any memory
// meaning that an EvalStringBuilder that is constantly reused will be very
// unlikely to allocate.

namespace {

const EvalString::Offset leadingBit =
    static_cast<EvalString::Offset>(1)
    << (std::numeric_limits<EvalString::Offset>::digits - 1);

EvalString::Offset clearLeadingBit(EvalString::Offset in) {
  return in & ~leadingBit;
}

EvalString::Offset setLeadingBit(EvalString::Offset in) {
  return in | leadingBit;
}

bool hasLeadingBit(EvalString::Offset in) {
  return in >> (std::numeric_limits<EvalString::Offset>::digits - 1U);
}

void appendSegment(std::string& str,
                   const EvalString::Offset length,
                   const StringPiece& text) {
#if __cpp_lib_string_resize_and_overwrite >= 202110L
  str.resize_and_overwrite(
      str.size() + sizeof(length) + text.size(),
      [oldSize = str.size(), length, text](char* start, std::size_t) noexcept {
        char* out = start + oldSize;
        out = std::copy_n(reinterpret_cast<const char*>(&length),
                          sizeof(length), out);
        out = std::copy(text.begin(), text.end(), out);
        return out - start;
      });
#else
  str.append(reinterpret_cast<const char*>(&length), sizeof(length));
  str.append(text.str_, text.len_);
#endif
}

}  // namespace

EvalString::const_iterator::const_iterator() : pos_(nullptr) {}

EvalString::const_iterator::const_iterator(const char* pos) : pos_(pos) {}

std::pair<StringPiece, EvalString::TokenType>
EvalString::const_iterator::operator*() const {
  Offset length;
  std::copy_n(pos_, sizeof(length), reinterpret_cast<char*>(&length));
  return {{pos_ + sizeof(length), clearLeadingBit(length)},
          static_cast<TokenType>(hasLeadingBit(length))};
}

EvalString::const_iterator& EvalString::const_iterator::operator++() {
  Offset length;
  std::copy_n(pos_, sizeof(length), reinterpret_cast<char*>(&length));
  pos_ += sizeof(length) + clearLeadingBit(length);
  return *this;
}

EvalString::const_iterator EvalString::const_iterator::operator++(int) {
  const const_iterator copy = *this;
  ++*this;
  return copy;
}

bool operator==(EvalString::const_iterator lhs,
                EvalString::const_iterator rhs) {
  return lhs.pos_ == rhs.pos_;
}

bool operator!=(EvalString::const_iterator lhs,
                EvalString::const_iterator rhs) {
  return !(lhs == rhs);
}

EvalString::EvalString() = default;

bool EvalString::empty() const {
  return data_.empty();
}

EvalString::const_iterator EvalString::begin() const {
  return const_iterator(data_.data());
}

EvalString::const_iterator EvalString::end() const {
  return const_iterator(data_.data() + data_.size());
}

std::string EvalString::Serialize() const {
  std::string result;
  for (const auto& token : *this) {
    result.append("[");
    if (token.second == TokenType::SPECIAL) {
      result.append("$");
    }
    result.append(token.first.str_, token.first.len_);
    result.append("]");
  }
  return result;
}

std::string EvalString::Unparse() const {
  std::string result;
  for (const auto& token : *this) {
    const bool special = (token.second == TokenType::SPECIAL);
    if (special) {
      result.append("${");
    }
    result.append(token.first.str_, token.first.len_);
    if (special) {
      result.append("}");
    }
  }
  return result;
}

EvalStringBuilder::EvalStringBuilder() = default;

bool EvalStringBuilder::empty() const {
  return str_.empty();
}

void EvalStringBuilder::Clear() {
  str_.data_.clear();
  lastTextSegmentLength_ = 0;
}

void EvalStringBuilder::AddText(StringPiece text) {
  assert(!text.empty());
  if (lastTextSegmentLength_ > 0) {
    // If the last part was raw text we can extend it.
    const EvalString::Offset newLength = lastTextSegmentLength_ + text.size();
    std::copy_n(reinterpret_cast<const char*>(&newLength), sizeof(newLength),
                &str_.data_[str_.data_.size() - sizeof(EvalString::Offset) -
                            lastTextSegmentLength_]);
    str_.data_.append(text.str_, text.len_);
    lastTextSegmentLength_ = newLength;
  } else {
    // Otherwise write new token.
    const EvalString::Offset length = text.size();
    appendSegment(str_.data_, length, text);
    lastTextSegmentLength_ = length;
  }
}

void EvalStringBuilder::AddSpecial(StringPiece text) {
  assert(!text.empty());
  appendSegment(str_.data_, setLeadingBit(text.size()), text);
  lastTextSegmentLength_ = 0;
}

const EvalString& EvalStringBuilder::str() const & {
  return str_;
}

EvalStringBuilder::operator const EvalString&() const {
  return str_;
}

EvalString EvalStringBuilder::str() && {
  return std::move(str_);
}

