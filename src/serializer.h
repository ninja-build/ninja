// Copyright 2016 Google Inc. All Rights Reserved.
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

#ifndef NINJA_SERIALIZER_H_
#define NINJA_SERIALIZER_H_

#include <stdio.h>

#include <map>
#include <string>
#include <vector>

#include "state.h"
#include "string_piece.h"

/// A serializer of ninja state loaded from the manifest parser.
/// See serializer.cc for the detail of the binary format.
class Serializer {
 public:
  explicit Serializer(const char* filename);
  ~Serializer();

  // This function modifies |id_| of nodes in |state|.
  bool SerializeState(const State& state);

 private:
  // Copy data from |state| and give them IDs. All functions below
  // should be called after calling this function.
  void CollectData(const State& state);

  bool SerializePools(const map<string, Pool*>& pools);
  bool SerializeBindings();
  bool SerializePaths(const State::Paths& paths);
  bool SerializeRules();
  bool SerializeEdges(const vector<Edge*>& edges);
  bool SerializeDefaults(const vector<Node*>& defaults);

  bool SerializeInt(int v);
  bool SerializeString(StringPiece s);

  FILE* fp_;

  map<const Pool*, int> pool_ids_;
  vector<const BindingEnv*> bindings_;
  map<const BindingEnv*, int> binding_ids_;
  vector<const Rule*> rules_;
  map<const Rule*, int> rule_ids_;
};

/// A deserializer of ninja state loaded from the manifest parser.
/// See serializer.cc for the detail of the binary format.
class Deserializer {
 public:
  explicit Deserializer(const char* filename);
  ~Deserializer();

  bool DeserializeState(State* state);

  int DeserializeInt();
  bool DeserializeString(string* s);

 private:
  bool DeserializePools(map<string, Pool*>* pools);
  bool DeserializeBindings(BindingEnv* bindings);
  bool DeserializePaths(State::Paths* paths);
  bool DeserializeRules();
  bool DeserializeEdges(vector<Edge*>* edges);
  bool DeserializeDefaults(vector<Node*>* defaults);

  Node* DeserializeNode();

  FILE* fp_;

  vector<Pool*> pools_;
  vector<BindingEnv*> bindings_;
  vector<Node*> nodes_;
  vector<Rule*> rules_;
};

#endif  // NINJA_SERIALIZER_H_
