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

// Ninja's binary manifests have integer and string primitive
// values. An integer primitive consumes sizeof(int) bytes and encoded
// in the machine's endian, so binary manifests aren't portable. A
// string primitive is encoded by an integer value followed by some
// characters. The first integer is the length of the
// string. Similarly, collection types are encoded by a number
// followed by members. In ninja's State object, pools, binding
// environments, nodes, and rules are referenced by pointers. Each of
// them is given an ID when it appears, and is referenced by the ID.

#include "serializer.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "graph.h"
#include "metrics.h"
#include "state.h"
#include "util.h"

#define SERIALIZE_INT(x) if (!SerializeInt(x)) return false
#define SERIALIZE_STRING(x) if (!SerializeString(x)) return false

static const char kBinaryFormatMagic[] = "binja";
static const int kBinaryFormatVersion = 1;

Serializer::Serializer(const char* filename) {
  fp_ = fopen(filename, "wb");
  if (!fp_) {
    Error("%s: %s", filename, strerror(errno));
  }
}

Serializer::~Serializer() {
  if (fp_)
    fclose(fp_);
}

bool Serializer::SerializeState(const State& state) {
  METRIC_RECORD("serialize");
  if (!fp_)
    return false;

  SERIALIZE_STRING(kBinaryFormatMagic);
  SERIALIZE_INT(kBinaryFormatVersion);

  CollectData(state);
  if (!SerializePools(state.pools_))
    return false;
  if (!SerializeBindings())
    return false;
  if (!SerializePaths(state.paths_))
    return false;
  if (!SerializeRules())
    return false;
  if (!SerializeEdges(state.edges_))
    return false;
  if (!SerializeDefaults(state.defaults_))
    return false;

  SERIALIZE_INT(ftell(fp_));
  return true;
}

void Serializer::CollectData(const State& state) {
  METRIC_RECORD("serialize - collect data");
  bindings_.push_back(&state.bindings_);
  binding_ids_.insert(make_pair(&state.bindings_, 0));
  for (size_t i = 0; i < state.edges_.size(); ++i) {
    const BindingEnv* binding = state.edges_[i]->env_;
    int id = bindings_.size();
    if (binding_ids_.insert(make_pair(binding, id)).second) {
      bindings_.push_back(binding);
    }
  }

  for (size_t i = 0; i < state.edges_.size(); ++i) {
    const Edge* edge = state.edges_[i];
    if (edge->is_phony())
      continue;
    const Rule* rule = edge->rule_;
    int id = rules_.size();
    if (rule_ids_.insert(make_pair(rule, id)).second) {
      rules_.push_back(rule);
    }
  }
}

bool Serializer::SerializePools(const map<string, Pool*>& pools) {
  SERIALIZE_INT(pools.size());
  for (map<string, Pool*>::const_iterator it = pools.begin();
       it != pools.end(); ++it) {
    METRIC_RECORD("serialize - pool");
    const Pool* pool = it->second;
    SERIALIZE_STRING(pool->name());
    SERIALIZE_INT(pool->depth());
    if (!pool_ids_.insert(make_pair(pool, pool_ids_.size())).second) {
      Error("duplicate pool instances with different names");
      return false;
    }
  }
  return true;
}

bool Serializer::SerializeBindings() {
  SERIALIZE_INT(bindings_.size());
  for (size_t i = 0; i < bindings_.size(); i++) {
    METRIC_RECORD("serialize - binding");
    const map<string, string>& bindings = bindings_[i]->bindings();
    SERIALIZE_INT(bindings.size());
    for (map<string, string>::const_iterator it = bindings.begin();
         it != bindings.end(); ++it) {
      SERIALIZE_STRING(it->first);
      SERIALIZE_STRING(it->second);
    }
  }

  for (size_t i = 0; i < bindings_.size(); i++) {
    const BindingEnv* parent = bindings_[i]->parent();
    if (parent) {
      SERIALIZE_INT(binding_ids_[parent] + 1);
    } else {
      SERIALIZE_INT(0);
    }
  }
  return true;
}

bool Serializer::SerializePaths(const State::Paths& paths) {
  SERIALIZE_INT(paths.size());
  int node_id = 0;
  for (State::Paths::const_iterator it = paths.begin();
       it != paths.end(); ++it) {
    METRIC_RECORD("serialize - path");
    Node* node = it->second;
    SERIALIZE_STRING(node->path());
    SERIALIZE_INT(node->slash_bits());
    node->set_id(node_id++);
  }
  return true;
}

bool Serializer::SerializeRules() {
  SERIALIZE_INT(rules_.size());
  for (size_t i = 0; i < rules_.size(); i++) {
    METRIC_RECORD("serialize - rule");
    const Rule* rule = rules_[i];
    SERIALIZE_STRING(rule->name());

    const Rule::Bindings& bindings = rule->bindings();
    SERIALIZE_INT(bindings.size());
    for (Rule::Bindings::const_iterator it = bindings.begin();
         it != bindings.end(); ++it) {
      SERIALIZE_STRING(it->first);
      const EvalString& es = it->second;
      SERIALIZE_INT(es.parsed_.size());
      for (size_t i = 0; i < es.parsed_.size(); i++) {
        SERIALIZE_STRING(es.parsed_[i].first);
        SERIALIZE_INT(es.parsed_[i].second);
      }
    }
  }
  return true;
}

bool Serializer::SerializeEdges(const vector<Edge*>& edges) {
  SERIALIZE_INT(edges.size());
  for (size_t i = 0; i < edges.size(); ++i) {
    METRIC_RECORD("serialize - edge");
    const Edge* edge = edges[i];
    if (edge->is_phony()) {
      SERIALIZE_INT(0);
    } else {
      SERIALIZE_INT(rule_ids_.at(edge->rule_) + 1);
    }

    SERIALIZE_INT(pool_ids_.at(edge->pool_));

    SERIALIZE_INT(edge->inputs_.size());
    for (size_t i = 0; i < edge->inputs_.size(); ++i) {
      SERIALIZE_INT(edge->inputs_[i]->id());
    }

    SERIALIZE_INT(edge->outputs_.size());
    for (size_t i = 0; i < edge->outputs_.size(); ++i) {
      SERIALIZE_INT(edge->outputs_[i]->id());
    }

    SERIALIZE_INT(edge->implicit_deps_);
    SERIALIZE_INT(edge->order_only_deps_);

    if (!edge->env_) {
      Error("no |env_| for a edge");
      return false;
    }
    SERIALIZE_INT(binding_ids_.at(edge->env_));
  }
  return true;
}

bool Serializer::SerializeDefaults(const vector<Node*>& defaults) {
  SERIALIZE_INT(defaults.size());
  for (size_t i = 0; i < defaults.size(); ++i) {
    METRIC_RECORD("serialize - default");
    SERIALIZE_INT(defaults[i]->id());
  }
  return true;
}

bool Serializer::SerializeInt(int v) {
  ssize_t r = fwrite(&v, sizeof(v), 1, fp_);
  if (r < 0) {
    Error("failed to serialize an int: %s", strerror(errno));
    return false;
  }
  if (r != 1) {
    Error("failed to serialize an int: %zd", r);
    return false;
  }
  return true;
}

bool Serializer::SerializeString(StringPiece s) {
  SERIALIZE_INT(s.len_);
  ssize_t r = fwrite(s.str_, 1, s.len_, fp_);
  if (r < 0) {
    Error("failed to serialize a string: %s", strerror(errno));
    return false;
  }
  if (r != static_cast<ssize_t>(s.len_)) {
    Error("failed to serialize a string: %zd", r);
    return false;
  }
  return true;
}

Deserializer::Deserializer(const char* filename) {
  fp_ = fopen(filename, "rb");
  if (!fp_) {
    Error("%s: %s", filename, strerror(errno));
  }
}

Deserializer::~Deserializer() {
  if (fp_)
    fclose(fp_);
}

bool Deserializer::DeserializeState(State* state) {
  METRIC_RECORD("deserialize");
  if (!fp_)
    return false;

  string magic;
  if (!DeserializeString(&magic))
    return false;
  if (magic != kBinaryFormatMagic) {
    Error("not ninja binary format");
    return false;
  }
  if (DeserializeInt() != kBinaryFormatVersion) {
    Error("wrong ninja binary version");
    return false;
  }

  if (!DeserializePools(&state->pools_))
    return false;
  if (!DeserializeBindings(&state->bindings_))
    return false;
  if (!DeserializePaths(&state->paths_))
    return false;
  if (!DeserializeRules())
    return false;
  if (!DeserializeEdges(&state->edges_))
    return false;
  if (!DeserializeDefaults(&state->defaults_))
    return false;

  int len = ftell(fp_);
  if (len != DeserializeInt()) {
    Error("broken ninja binary data");
    return false;
  }
  return true;
}

bool Deserializer::DeserializePools(map<string, Pool*>* pools) {
  // Remove default pools.
  pools->clear();
  int pool_size = DeserializeInt();
  if (pool_size < 0)
    return false;
  string buf;
  for (int i = 0; i < pool_size; i++) {
    METRIC_RECORD("deserialize - pool");
    if (!DeserializeString(&buf))
      return false;
    int depth = DeserializeInt();
    if (depth < 0)
      return false;

    Pool* pool = new Pool(buf, depth);
    pools_.push_back(pool);
    if (!pools->insert(make_pair(pool->name(), pool)).second) {
      Error("duplicate pool name: %s", pool->name().c_str());
      return false;
    }
  }
  return true;
}

bool Deserializer::DeserializeBindings(BindingEnv* bindings) {
  int bindings_size = DeserializeInt();
  if (bindings_size < 0)
    return false;
  for (int i = 0; i < bindings_size; i++) {
    METRIC_RECORD("deserialize - binding");
    BindingEnv* b = i ? new BindingEnv() : bindings;
    int bindings_size = DeserializeInt();
    if (bindings_size < 0)
      return false;
    string k, v;
    for (int i = 0; i < bindings_size; ++i) {
      if (!DeserializeString(&k))
        return false;
      if (!DeserializeString(&v))
        return false;
      b->AddBinding(k, v);
    }
    bindings_.push_back(b);
  }
  for (int i = 0; i < bindings_size; i++) {
    int parent_id = DeserializeInt();
    if (parent_id < 0)
      return false;
    if (parent_id) {
      parent_id--;
      if (parent_id >= static_cast<int>(bindings_.size())) {
        Error("parent ID overflow %d vs %zu\n", parent_id, bindings_.size());
        return false;
      }
      bindings_[i]->set_parent(bindings_[parent_id]);
    }
  }
  return true;
}

bool Deserializer::DeserializePaths(State::Paths* paths) {
  int path_size = DeserializeInt();
  if (path_size < 0)
    return false;
  string buf;
  for (int i = 0; i < path_size; ++i) {
    METRIC_RECORD("deserialize - path");
    if (!DeserializeString(&buf))
      return false;
    int slash_bits = DeserializeInt();
    if (slash_bits < 0)
      return false;

    Node* node = new Node(buf, slash_bits);
    nodes_.push_back(node);
    if (!paths->insert(make_pair(StringPiece(node->path()), node)).second) {
      Error("duplicate path name: %s", node->path().c_str());
      return false;
    }
  }
  return true;
}

bool Deserializer::DeserializeRules() {
  int rule_size = DeserializeInt();
  if (rule_size < 0)
    return false;
  string buf, buf2;
  for (int i = 0; i < rule_size; ++i) {
    METRIC_RECORD("deserialize - rule");
    if (!DeserializeString(&buf))
      return false;
    Rule* rule = new Rule(buf);
    rules_.push_back(rule);

    int binding_size = DeserializeInt();
    if (binding_size < 0)
      return false;
    for (int j = 0; j < binding_size; j++) {
      if (!DeserializeString(&buf))
        return false;
      EvalString es;
      int size = DeserializeInt();
      if (size < 0)
        return false;
      for (int i = 0; i < size; i++) {
        if (!DeserializeString(&buf2))
          return false;
        int type = DeserializeInt();
        if (type != EvalString::RAW && type != EvalString::SPECIAL)
          return false;
        es.parsed_.push_back(
            make_pair(buf2, static_cast<EvalString::TokenType>(type)));
      }
      rule->AddBinding(buf, es);
    }
  }
  return true;
}

bool Deserializer::DeserializeEdges(vector<Edge*>* edges) {
  int edge_size = DeserializeInt();
  if (edge_size < 0)
    return false;

  for (int i = 0; i < edge_size; ++i) {
    METRIC_RECORD("deserialize edges");
    Edge* edge = new Edge();
    edges->push_back(edge);

    int rule_id = DeserializeInt();
    if (rule_id < 0)
      return false;

    if (rule_id == 0) {
      edge->rule_ = &State::kPhonyRule;
    } else {
      rule_id--;
      if (rule_id >= static_cast<int>(rules_.size())) {
        Error("rule ID overflow %d vs %zu\n", rule_id, rules_.size());
        return false;
      }
      edge->rule_ = rules_[rule_id];
    }

    int pool_id = DeserializeInt();
    if (pool_id < 0)
      return false;
    if (pool_id >= static_cast<int>(pools_.size())) {
      Error("pool ID overflow %d vs %zu\n", pool_id, pools_.size());
      return false;
    }
    edge->pool_ = pools_[pool_id];

    int input_size = DeserializeInt();
    if (input_size < 0)
      return false;
    for (int j = 0; j < input_size; j++) {
      Node* node = DeserializeNode();
      if (!node)
        return false;
      node->AddOutEdge(edge);
      edge->inputs_.push_back(node);
    }

    int output_size = DeserializeInt();
    if (output_size < 0)
      return false;
    for (int j = 0; j < output_size; j++) {
      Node* node = DeserializeNode();
      if (!node)
        return false;
      node->set_in_edge(edge);
      edge->outputs_.push_back(node);
    }

    int implicit_deps = DeserializeInt();
    if (implicit_deps < 0)
      return false;
    edge->implicit_deps_ = implicit_deps;

    int order_only_deps = DeserializeInt();
    if (order_only_deps < 0)
      return false;
    edge->order_only_deps_ = order_only_deps;

    int binding_id = DeserializeInt();
    if (binding_id < 0)
      return false;
    if (binding_id >= static_cast<int>(bindings_.size())) {
      Error("binding ID overflow %d vs %zu\n", binding_id, bindings_.size());
      return false;
    }
    edge->env_ = bindings_[binding_id];
  }
  return true;
}

bool Deserializer::DeserializeDefaults(vector<Node*>* defaults) {
  int default_size = DeserializeInt();
  if (default_size < 0)
    return false;
  for (int i = 0; i < default_size; ++i) {
    Node* node = DeserializeNode();
    if (!node)
      return false;
    defaults->push_back(node);
  }
  return true;
}

Node* Deserializer::DeserializeNode() {
  int node_id = DeserializeInt();
  if (node_id < 0)
    return NULL;
  if (node_id >= static_cast<int>(nodes_.size())) {
    Error("node ID overflow %d vs %zu\n", node_id, nodes_.size());
    return NULL;
  }
  return nodes_[node_id];
}

int Deserializer::DeserializeInt() {
  int v;
  ssize_t r = fread(&v, sizeof(v), 1, fp_);
  if (r < 0) {
    Error("failed to deserialize an int: %s", strerror(errno));
    return -1;
  }
  if (r != 1) {
    Error("failed to deserialize an int: %zd", r);
    return -1;
  }
  return v;
}

bool Deserializer::DeserializeString(string* s) {
  int len = DeserializeInt();
  if (len < 0)
    return false;
  s->resize(len);
  ssize_t r = fread(&(*s)[0], 1, s->size(), fp_);
  if (r < 0) {
    Error("failed to deserialize a string: %s", strerror(errno));
    return false;
  }
  if (r != static_cast<ssize_t>(s->size())) {
    Error("failed to deserialize a string: %zd", r);
    return false;
  }
  return true;
}
