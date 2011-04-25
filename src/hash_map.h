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

#ifdef _MSC_VER
#include <hash_map>

using stdext::hash_map;

#else

#include <ext/hash_map>

using __gnu_cxx::hash_map;

namespace __gnu_cxx {
template<>
struct hash<std::string> {
  size_t operator()(const std::string& s) const {
    return hash<const char*>()(s.c_str());
  }
};
}

#endif

#endif // NINJA_MAP_H_

