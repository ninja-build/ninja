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
#include <string>
#include <vector>

struct DiskInterface;
struct Edge;
struct Node;
struct State;

/// Store dynamically-discovered dependency information for one edge.
struct Dyndeps {
  Dyndeps() : used_(false), restat_(false) {}
  bool used_;
  bool restat_;
  std::vector<Node*> implicit_inputs_;
  std::vector<Node*> implicit_outputs_;
};

/// Store data loaded from one dyndep file.  Map from an edge
/// to its dynamically-discovered dependency information.
/// This is a struct rather than a typedef so that we can
/// forward-declare it in other headers.
struct DyndepFile: public std::map<Edge*, Dyndeps> {};

/// DyndepLoader loads dynamically discovered dependencies, as
/// referenced via the "dyndep" attribute in build files.
struct DyndepLoader {
  DyndepLoader(State* state, DiskInterface* disk_interface)
      : state_(state), disk_interface_(disk_interface) {}

  /// Load a dyndep file from the given node's path and update the
  /// build graph with the new information.  One overload accepts
  /// a caller-owned 'DyndepFile' object in which to store the
  /// information loaded from the dyndep file.
  bool LoadDyndeps(Node* node, std::string* err) const;
  bool LoadDyndeps(Node* node, DyndepFile* ddf, std::string* err) const;

 private:
  bool LoadDyndepFile(Node* file, DyndepFile* ddf, std::string* err) const;

  bool UpdateEdge(Edge* edge, Dyndeps const* dyndeps, std::string* err) const;

  State* state_;
  DiskInterface* disk_interface_;
};

#endif  // NINJA_DYNDEP_LOADER_H_
