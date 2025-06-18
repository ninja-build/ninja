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

#include <algorithm>
#include <string.h>
#include "string_piece.h"
#include "util.h"

#include "third_party/emhash/hash_table8.hpp"
#include "third_party/rapidhash/rapidhash.h"

namespace std {
template<>
struct hash<StringPiece> {
  typedef StringPiece argument_type;
  typedef size_t result_type;

  size_t operator()(StringPiece key) const {
    return rapidhash(key.str_, key.len_);
  }
};
}

/// A template for hash_maps keyed by a StringPiece whose string is
/// owned externally (typically by the values).  Use like:
/// ExternalStringHash<Foo*>::Type foos; to make foos into a hash
/// mapping StringPiece => Foo*.
template<typename V>
struct ExternalStringHashMap {
  typedef emhash8::HashMap<StringPiece, V> Type;
};

#endif // NINJA_MAP_H_
