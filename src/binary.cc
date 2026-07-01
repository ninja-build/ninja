// Copyright 2026 Google Inc. All Rights Reserved.
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

#include "binary.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#include "eval_env.h"
#include "graph.h"
#include "hash_map.h"
#include "metrics.h"
#include "state.h"

namespace {

/// Increment this whenever the binary format changes incompatibly.
constexpr uint16_t kBinaryFormatVersion = 1;

// Thin wrapper holding a const reference to T. operator<< writes the value's
// raw bytes to the stream (little-endian on x86, matching the host byte order).
template <class T>
struct Bin {
  explicit Bin(const T& value) : value_(value) {}
  const T& value_;
};

// BinVal stores by value so typed helpers can safely cast from a temporary.
template <class T>
struct BinVal {
  explicit BinVal(T value) : value_(value) {}
  T value_;
};

template <class T>
std::ostream& operator<<(std::ostream& os, const BinVal<T>& a) {
  os.write(reinterpret_cast<const char*>(&a.value_), sizeof(a.value_));
  return os;
}

template <class U> BinVal<int8_t>   binInt8  (U v) { return BinVal<int8_t>  (static_cast<int8_t>  (v)); }
template <class U> BinVal<uint8_t>  binUInt8 (U v) { return BinVal<uint8_t> (static_cast<uint8_t> (v)); }
template <class U> BinVal<uint16_t> binUInt16(U v) { return BinVal<uint16_t>(static_cast<uint16_t>(v)); }
template <class U> BinVal<int32_t>  binInt32 (U v) { return BinVal<int32_t> (static_cast<int32_t> (v)); }
template <class U> BinVal<uint32_t> binUInt32(U v) { return BinVal<uint32_t>(static_cast<uint32_t>(v)); }
template <class U> BinVal<uint64_t> binUInt64(U v) { return BinVal<uint64_t>(static_cast<uint64_t>(v)); }

// Writes a length-prefixed string: uint32_t byte count followed by raw bytes.
struct StringBin {
  explicit StringBin(std::string_view text) : text_(text) {}
  std::string_view text_;
};

std::ostream& operator<<(std::ostream& os, const StringBin& s) {
  os << binUInt32(s.text_.size());
  os.write(s.text_.data(), s.text_.size());
  return os;
}

// Writes an EvalString token list: uint32_t count, then for each token a
// length-prefixed string followed by a type byte ('R' = raw, 'S' = special).
using TokenListBin = Bin<EvalString::TokenList>;

std::ostream& operator<<(std::ostream& os, const TokenListBin& s) {
  os << binUInt32(s.value_.size());
  for (const auto& token : s.value_)
    os << binUInt8(token.second == EvalString::RAW ? 'R' : 'S')
       << StringBin(token.first);
  return os;
}

// Writes Rule::Bindings: uint32_t count, then for each entry:
//   key (string), flag byte (0 = single string, 1 = token list), value.
using BindingsBin = Bin<Rule::Bindings>;

std::ostream& operator<<(std::ostream& os, const BindingsBin& s) {
  os << binUInt32(s.value_.size());
  for (const auto& entry : s.value_) {
    os << StringBin(entry.first);
    if (entry.second.IsSingle()) {
      os << binUInt8(0) << StringBin(entry.second.Single());
    } else {
      os << binUInt8(1) << TokenListBin(entry.second.SingleToken());
    }
  }
  return os;
}

// Writes a Rule: name string followed by its bindings.
using RuleBin = Bin<Rule>;

std::ostream& operator<<(std::ostream& os, const RuleBin& s) {
  return os << StringBin(s.value_.name())
            << BindingsBin(s.value_.GetBindings());
}

// Writes a Pool: name string followed by int32_t depth.
using PoolBin = Bin<Pool>;

std::ostream& operator<<(std::ostream& os, const PoolBin& s) {
  return os << StringBin(s.value_.name()) << binInt32(s.value_.depth());
}

/// std::map wrapper whose try_emplace asserts that the key is new.
template <class K, class V>
struct UniqueMap : std::map<K, V> {
  template <class Key2, class... Args>
  void try_emplace(Key2&& key, Args&&... args) {
    [[maybe_unused]] const auto [_, ok] = std::map<K, V>::try_emplace(
        std::forward<Key2>(key), std::forward<Args>(args)...);
    assert(ok);
  }
};

/// Serializes a build State to a binary stream.
class Dump {
 public:
  static void File(std::ostream& os, const State* state);

 private:
  Dump(std::ostream& os, const State* state) : os_(os), state_(state) {}

  void DumpToFile();
  void DumpToFile(const BindingEnv& env);
  void DumpEdges();
  void DumpPaths();
  void DumpDefaults();
  void DumpPools();

  std::ostream& os_;
  const State* state_;

  // Maps from object pointer to its byte offset in the output stream.
  // Used to emit back-references instead of duplicating shared objects.
  UniqueMap<const Rule*, uint64_t> rule_offsets_;
  UniqueMap<const Pool*, uint64_t> pool_offsets_;
  UniqueMap<const Node*, uint64_t> node_offsets_;
  UniqueMap<const BindingEnv*, uint64_t> env_offsets_;
};

// Writes user-defined pools into pool_offsets_ (built-ins are pre-seeded as
// sentinels and skipped here). Offsets 0 and 1 are reserved for
// kDefaultPool/kConsolePool.
void Dump::DumpPools() {
  const auto& pools = state_->pools_;
  // Count user-defined pools; built-ins are already in pool_offsets_ as
  // sentinels (added by the caller before DumpPools), so they are excluded.
  uint32_t count = 0;
  for (const auto& entry : pools)
    if (pool_offsets_.find(entry.second) == pool_offsets_.end())
      ++count;

  os_ << binUInt32(count);
  for (const auto& entry : pools) {
    if (pool_offsets_.find(entry.second) != pool_offsets_.end())
      continue;
    pool_offsets_.try_emplace(entry.second, static_cast<uint64_t>(os_.tellp()));
    os_ << PoolBin(*entry.second);
  }
}

void Dump::DumpPaths() {
  const auto& paths = state_->paths_;
  os_ << binUInt32(paths.size());
  for (const auto& entry : paths) {
    node_offsets_.try_emplace(entry.second, static_cast<uint64_t>(os_.tellp()));
    os_ << StringBin(std::string_view(entry.first.str_, entry.first.len_))
        << binUInt64(entry.second->slash_bits());
  }
}

void Dump::DumpEdges() {
  const auto& edges = state_->edges_;
  os_ << binUInt32(edges.size());

  for (const Edge* edge : edges) {
    // dyndep node offset (0 if none)
    if (edge->dyndep_) {
      auto it = node_offsets_.find(edge->dyndep_);
      assert(it != node_offsets_.end());
      os_ << binUInt64(it->second);
    } else {
      os_ << binUInt64(0);
    }

    // env: 'd' = defined inline here, 'r' = back-reference by offset
    auto env_it = env_offsets_.find(edge->env_);
    if (env_it != env_offsets_.end()) {
      os_ << binInt8('r') << binUInt64(env_it->second);
    } else {
      os_ << binInt8('d');
      DumpToFile(*edge->env_);
    }

    // rule offset (0 reserved for phony)
    if (edge->rule_->IsPhony()) {
      os_ << binUInt64(0);
    } else {
      auto rule_it = rule_offsets_.find(edge->rule_);
      assert(rule_it != rule_offsets_.end());
      os_ << binUInt64(rule_it->second);
    }

    // pool offset
    {
      const auto pool_it = pool_offsets_.find(edge->pool_);
      assert(pool_it != pool_offsets_.end());
      os_ << binUInt64(pool_it->second);
    }

    // outputs
    os_ << binInt32(edge->implicit_outs_) << binUInt32(edge->outputs_.size());
    for (const Node* out : edge->outputs_) {
      auto it = node_offsets_.find(out);
      assert(it != node_offsets_.end());
      os_ << binUInt64(it->second);
    }

    // inputs
    os_ << binInt32(edge->implicit_deps_) << binInt32(edge->order_only_deps_)
        << binUInt32(edge->inputs_.size());
    for (const Node* in : edge->inputs_) {
      auto it = node_offsets_.find(in);
      assert(it != node_offsets_.end());
      os_ << binUInt64(it->second);
    }

    // validations
    os_ << binUInt32(edge->validations_.size());
    for (const Node* v : edge->validations_) {
      auto it = node_offsets_.find(v);
      assert(it != node_offsets_.end());
      os_ << binUInt64(it->second);
    }
  }
}

void Dump::File(std::ostream& os, const State* state) {
  Dump dump(os, state);
  dump.DumpToFile();
}

void Dump::DumpDefaults() {
  const auto& defaults = state_->defaults_;
  os_ << binUInt32(defaults.size());
  for (const Node* node : defaults) {
    const auto it = node_offsets_.find(node);
    assert(it != node_offsets_.end());
    os_ << binUInt64(it->second);
  }
}

// Writes a BindingEnv recursively.
//
// Parent encoding (1 byte):
//   '0' = no parent (root env)
//   '1' = parent follows inline (not yet seen)
//   '2' = parent already written; followed by its uint64_t stream offset
//
// Phony is excluded from the rule list because State::State() always adds it.
void Dump::DumpToFile(const BindingEnv& env) {
  const auto& bindings = env.GetBindings();
  const auto& rules = env.GetRules();

  const BindingEnv* parent = env.GetParent();
  if (parent) {
    auto it = env_offsets_.find(parent);
    if (it == env_offsets_.end()) {
      os_ << binInt8('1');
      DumpToFile(*parent);
    } else {
      os_ << binInt8('2');
      os_ << binUInt64(it->second);
    }
  } else {
    os_ << binInt8('0');
  }
  env_offsets_.try_emplace(&env, static_cast<uint64_t>(os_.tellp()));

  // bindings
  os_ << binUInt32(bindings.size());
  for (const auto& b : bindings)
    os_ << StringBin(b.first) << StringBin(b.second);

  // rules (phony excluded — State::State() always re-adds it on load)
  uint32_t non_phony_count = 0;
  for (const auto& r : rules)
    if (!r.second->IsPhony())
      ++non_phony_count;

  os_ << binUInt32(non_phony_count);
  for (const auto& r : rules) {
    if (r.second->IsPhony())
      continue;
    rule_offsets_.try_emplace(r.second.get(),
                              static_cast<uint64_t>(os_.tellp()));
    os_ << RuleBin(*r.second);
  }
}

void Dump::DumpToFile() {
  METRIC_RECORD("write binary manifest");
  os_ << binUInt16(kBinaryFormatVersion);
  // integrity check
  os_ << binUInt64(
      rapidhash(reinterpret_cast<const void*>(&kBinaryFormatVersion), 2));

  // Sentinel offsets for built-in pools (never written to file, like phony).
  pool_offsets_.try_emplace(&state_->kDefaultPool, uint64_t(0));
  pool_offsets_.try_emplace(&state_->kConsolePool, uint64_t(1));
  DumpToFile(state_->bindings_);
  DumpPools();
  DumpPaths();
  DumpEdges();
  DumpDefaults();
}

template <class T>
T ReadBin(std::istream& is) {
  T value{};
  is.read(reinterpret_cast<char*>(&value), sizeof(value));
  return value;
}

inline uint8_t  ReadU8 (std::istream& is) { return ReadBin<uint8_t>(is);  }
inline uint16_t ReadU16(std::istream& is) { return ReadBin<uint16_t>(is); }
inline uint32_t ReadU32(std::istream& is) { return ReadBin<uint32_t>(is); }
inline uint64_t ReadU64(std::istream& is) { return ReadBin<uint64_t>(is); }
inline int8_t   ReadI8 (std::istream& is) { return ReadBin<int8_t>(is);   }
inline int32_t  ReadI32(std::istream& is) { return ReadBin<int32_t>(is);  }

std::string ReadString(std::istream& is) {
  const uint32_t len = ReadU32(is);
  std::string s(len, '\0');
  is.read(s.data(), len);
  return s;
}

// Reads a binding value written by BindingsBin: flag byte selects
// single-string (0) or token-list (1) encoding.
EvalString ReadEvalString(std::istream& is) {
  const uint8_t flag = ReadU8(is);
  EvalString eval;
  if (flag == 0) {
    eval.AddText(ReadString(is));
  } else {
    const uint32_t count = ReadU32(is);
    for (uint32_t i = 0; i < count; ++i) {
      const uint8_t type = ReadU8(is);
      if (type == 'R')
        eval.AddText(ReadString(is));
      else
        eval.AddSpecial(ReadString(is));
    }
  }
  return eval;
}

bool AddOut(Edge* edge, Node* node) {
  const bool ok = !node->in_edge();

  edge->outputs_.push_back(node);
  node->set_in_edge(edge);
  node->set_generated_by_dep_loader(false);
  return ok;
}

void AddIn(Edge* edge, Node* node) {
  node->set_generated_by_dep_loader(false);
  edge->inputs_.push_back(node);
  node->AddOutEdge(edge);
}

void AddValidation(Edge* edge, Node* node) {
  node->set_generated_by_dep_loader(false);
  edge->validations_.push_back(node);
  node->AddValidationOutEdge(edge);
}

/// Deserializes a build State from a binary stream written by Dump::DumpToFile.
class Read {
 public:
  /// Returns false if the file has an unrecognised format version.
  static bool File(std::istream& is, State* state);

 private:
  Read(std::istream& is, State* state);

  BindingEnv* ReadBindingEnv();
  std::unique_ptr<Rule> ReadRule();
  void ReadPool();
  void ReadNode();
  void ReadEdge();
  void ReadDefaults();

  std::istream& is_;
  State* state_;

  // Maps from byte offset in the stream to the deserialized object.
  // Used to resolve back-references.
  UniqueMap<uint64_t, Node*> nodes_;
  UniqueMap<uint64_t, const Rule*> rules_by_offset_;
  UniqueMap<uint64_t, Pool*> pools_by_offset_;
  UniqueMap<uint64_t, BindingEnv*> envs_by_offset_;

#ifndef NDEBUG
  bool bindingEnv_root = false;
#endif
};

bool Read::File(std::istream& is, State* state) {
  const uint16_t version = ReadU16(is);
  const uint64_t integrity = ReadU64(is);
  if (version != kBinaryFormatVersion ||
      integrity !=
          rapidhash(reinterpret_cast<const void*>(&kBinaryFormatVersion), 2))
    return false;
  Read(is, state);
  return true;
}

void Read::ReadPool() {
  const uint32_t count = ReadU32(is_);

  for (uint32_t i = 0; i < count; ++i) {
    const auto offset = is_.tellg();
    const std::string name = ReadString(is_);
    const int32_t depth = ReadI32(is_);
    Pool* pool = new Pool(name, depth);
    state_->AddPool(pool);
    pools_by_offset_.try_emplace(static_cast<uint64_t>(offset), pool);
  }
}

std::unique_ptr<Rule> Read::ReadRule() {
  const auto offset = is_.tellg();
  std::string name = ReadString(is_);
  auto rule = std::make_unique<Rule>(name);
  rules_by_offset_.try_emplace(offset, rule.get());

  const uint32_t count = ReadU32(is_);
  for (uint32_t i = 0; i < count; ++i) {
    const std::string key = ReadString(is_);
    const EvalString val = ReadEvalString(is_);
    rule->AddBinding(key, val);
  }
  return rule;
}

// Reads a BindingEnv written by BindingsEnvBin.
// Parent tag byte:
//   '0' = root (no parent), populates state_->bindings_ in-place
//   '1' = parent, follows inline
//   '2' = parent, already read, uint64_t back-reference follows
BindingEnv* Read::ReadBindingEnv() {
  const int8_t parent_tag = ReadI8(is_);

  BindingEnv* env;
  if (parent_tag == '1') {
    BindingEnv* parent = ReadBindingEnv();
    env = new BindingEnv(parent);
  } else if (parent_tag == '2') {
    const uint64_t ref = ReadU64(is_);
    const auto it = envs_by_offset_.find(ref);
    assert(it != envs_by_offset_.end());
    env = new BindingEnv(it->second);
  } else {
    // '0': root env — read directly into state_->bindings_ (no allocation).
    // Safe because state_->bindings_ is always written first and edge envs
    // always reference it via '2', never re-encountering '0'.
    assert(!bindingEnv_root);
    assert((bindingEnv_root = true, true));
    env = &state_->bindings_;
  }

  envs_by_offset_.try_emplace(static_cast<uint64_t>(is_.tellg()), env);

  const uint32_t binding_count = ReadU32(is_);
  for (uint32_t i = 0; i < binding_count; ++i) {
    const std::string key = ReadString(is_);
    const std::string val = ReadString(is_);
    env->AddBinding(key, val);
  }

  const uint32_t rule_count = ReadU32(is_);
  for (uint32_t i = 0; i < rule_count; ++i) {
    auto rule = ReadRule();
    if (rule)
      env->AddRule(std::move(rule));
  }

  return env;
}

void Read::ReadNode() {
  const uint32_t count = ReadU32(is_);

  for (uint32_t i = 0; i < count; ++i) {
    const auto offset = is_.tellg();
    const std::string name = ReadString(is_);
    const uint64_t slash_bits = ReadU64(is_);
    Node* node = state_->AddNode(name, slash_bits);
    nodes_.try_emplace(static_cast<uint64_t>(offset), node);
  }
}

void Read::ReadEdge() {
  const uint32_t count = ReadU32(is_);

  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t dyndep_offset = ReadU64(is_);

    // env: 'd' = defined inline, 'r' = back-reference by offset
    const int8_t env_tag = ReadI8(is_);
    BindingEnv* env;
    if (env_tag == 'd') {
      env = ReadBindingEnv();
    } else {
      assert(env_tag == 'r');
      const uint64_t ref = ReadU64(is_);
      const auto it = envs_by_offset_.find(ref);
      assert(it != envs_by_offset_.end());
      env = it->second;
    }

    // rule (offset 0 is reserved for phony)
    const uint64_t rule_offset = ReadU64(is_);
    const auto rule_it = rules_by_offset_.find(rule_offset);
    assert(rule_it != rules_by_offset_.end());

    // pool
    const uint64_t pool_offset = ReadU64(is_);
    const auto pool_it = pools_by_offset_.find(pool_offset);
    assert(pool_it != pools_by_offset_.end());

    Edge* edge = state_->AddEdge(rule_it->second);
    edge->env_ = env;
    edge->pool_ = pool_it->second;

    if (dyndep_offset != 0) {
      const auto it = nodes_.find(dyndep_offset);
      assert(it != nodes_.end());
      edge->dyndep_ = it->second;
    }

    // outputs
    const int32_t implicit_outs = ReadI32(is_);
    const uint32_t out_count = ReadU32(is_);
    edge->implicit_outs_ = implicit_outs;
    for (uint32_t j = 0; j < out_count; ++j) {
      const uint64_t offset = ReadU64(is_);
      const auto it = nodes_.find(offset);
      assert(it != nodes_.end());
      [[maybe_unused]] const bool ok = AddOut(edge, it->second);
      assert(ok);
    }

    // inputs
    edge->implicit_deps_ = ReadI32(is_);
    edge->order_only_deps_ = ReadI32(is_);
    const uint32_t in_count = ReadU32(is_);
    for (uint32_t j = 0; j < in_count; ++j) {
      const uint64_t offset = ReadU64(is_);
      const auto it = nodes_.find(offset);
      assert(it != nodes_.end());
      AddIn(edge, it->second);
    }

    // validations
    const uint32_t val_count = ReadU32(is_);
    for (uint32_t j = 0; j < val_count; ++j) {
      const uint64_t offset = ReadU64(is_);
      const auto it = nodes_.find(offset);
      assert(it != nodes_.end());
      AddValidation(edge, it->second);
    }
  }
}

void Read::ReadDefaults() {
  const uint32_t count = ReadU32(is_);
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t offset = ReadU64(is_);
    const auto it = nodes_.find(offset);
    assert(it != nodes_.end());
    state_->defaults_.push_back(it->second);
  }
}

Read::Read(std::istream& is, State* state) : is_(is), state_(state) {
  METRIC_RECORD(".ninja parse binary");
  // Sentinel offsets for built-ins — never in the file, seeded like phony.
  rules_by_offset_[0] = state_->bindings_.LookupRule("phony");
  assert(rules_by_offset_[0]);
  pools_by_offset_[0] = &state_->kDefaultPool;
  pools_by_offset_[1] = &state_->kConsolePool;

  ReadBindingEnv();
  ReadPool();
  ReadNode();
  ReadEdge();
  ReadDefaults();
}

}  // namespace

bool ReadManifestCache(std::istream& is, State* state){
  return Read::File(is, state);
}

void WriteManifestCache(std::ostream& os, const State* state){
  Dump::File(os, state);
}
