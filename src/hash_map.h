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

#ifndef NINJA_MAP_H_
#define NINJA_MAP_H_

#include <string.h>

#include <algorithm>

#include "murmurhash2.h"
#include "string_piece.h"

static const unsigned int kHash2Seed = 0xDECAFBAD;

#if (__cplusplus >= 201103L) || (_MSC_VER >= 1900)
#include <unordered_map>

namespace std {
template<>
struct hash<StringPiece> {
  typedef StringPiece argument_type;
  typedef size_t result_type;

  size_t operator()(StringPiece key) const {
    return MurmurHash2(key.str_, key.len_, kHash2Seed);
  }
};
}

#elif defined(_MSC_VER)
#include <hash_map>

using stdext::hash_map;
using stdext::hash_compare;

struct StringPieceCmp : public hash_compare<StringPiece> {
  size_t operator()(const StringPiece& key) const {
    return MurmurHash2(key.str_, key.len_, kHash2Seed);
  }
  bool operator()(const StringPiece& a, const StringPiece& b) const {
    int cmp = strncmp(a.str_, b.str_, min(a.len_, b.len_));
    if (cmp < 0) {
      return true;
    } else if (cmp > 0) {
      return false;
    } else {
      return a.len_ < b.len_;
    }
  }
};

#else
#include <ext/hash_map>

using __gnu_cxx::hash_map;

namespace __gnu_cxx {
template<>
struct hash<StringPiece> {
  size_t operator()(StringPiece key) const {
    return MurmurHash2(key.str_, key.len_, kHash2Seed);
  }
};
}
#endif

/// A template for hash_maps keyed by a StringPiece whose string is
/// owned externally (typically by the values).  Use like:
/// ExternalStringHash<Foo*>::Type foos; to make foos into a hash
/// mapping StringPiece => Foo*.
template<typename V>
struct ExternalStringHashMap {
#if (__cplusplus >= 201103L) || (_MSC_VER >= 1900)
  typedef std::unordered_map<StringPiece, V> Type;
#elif defined(_MSC_VER)
  typedef hash_map<StringPiece, V, StringPieceCmp> Type;
#else
  typedef hash_map<StringPiece, V> Type;
#endif
};

#endif // NINJA_MAP_H_
