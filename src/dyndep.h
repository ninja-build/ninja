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

#ifndef NINJA_DYNDEP_LOADER_H_
#define NINJA_DYNDEP_LOADER_H_

#include <map>
#include <vector>

struct Edge;
struct Node;

/// Store dynamically-discovered dependency information for one edge.
struct Dyndeps {
  Dyndeps() : restat_(false) {}
  bool restat_;
  std::vector<Node*> implicit_inputs_;
  std::vector<Node*> implicit_outputs_;
};

/// Store data loaded from one dyndep file.  Map from an edge
/// to its dynamically-discovered dependency information.
/// This is a struct rather than a typedef so that we can
/// forward-declare it in other headers.
struct DyndepFile: public std::map<Edge*, Dyndeps> {};

#endif  // NINJA_DYNDEP_LOADER_H_
