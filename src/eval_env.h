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

#ifndef NINJA_EVAL_ENV_H_
#define NINJA_EVAL_ENV_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "evalstring.h"
#include "string_piece.h"
#include "string_piece_util.h"

struct Rule;

/// An interface for a scope for variable (e.g. "$foo") lookups.
struct Env {
  virtual ~Env() {}
  virtual std::string LookupVariable(const StringPiece& var) = 0;

  /// @return The evaluated string @a var expanded using values found in
  ///         environment @a env.
  template <typename E>
  static std::string Evaluate(E* env, const EvalString& var) {
    std::string result;
    const auto end = var.end();
    for (auto it = var.begin(); it != end; ++it) {
      std::pair<StringPiece, EvalString::TokenType> token = *it;
      switch (token.second) {
      case EvalString::TokenType::RAW:
        result.append(token.first.str_, token.first.len_);
        if (++it == end) {
          return result;
        }
        // As we consolidate consecutive text sections if we are not at the
        // end then we must have a variable next and we deliberately fallthrough
        token = *it;
        // fallthrough
      case EvalString::TokenType::SPECIAL: {
        result.append(env->LookupVariable(token.first));
      }
      }
    }
    return result;
  }
};

/// An invocable build command and associated metadata (description, etc.).
struct Rule {
  explicit Rule(const std::string& name) : name_(name) {}

  static std::unique_ptr<Rule> Phony();

  bool IsPhony() const;

  const std::string& name() const { return name_; }

  void AddBinding(const std::string& key, const EvalString& val);

  static bool IsReservedBinding(const StringPiece& var);

  const EvalString* GetBinding(const StringPiece& key) const;

 private:
  // Allow the parsers to reach into this object and fill out its fields.
  friend struct ManifestParser;

  std::string name_;
  typedef std::map<std::string, EvalString, StringPieceLess> Bindings;
  Bindings bindings_;
  bool phony_ = false;
};

/// An Env which contains a mapping of variables to values
/// as well as a pointer to a parent scope.
struct BindingEnv : public Env {
  BindingEnv() : parent_(NULL) {}
  explicit BindingEnv(BindingEnv* parent) : parent_(parent) {}

  virtual ~BindingEnv() {}
  virtual std::string LookupVariable(const StringPiece& var);

  void AddRule(std::unique_ptr<const Rule> rule);
  const Rule* LookupRule(const std::string& rule_name);
  const Rule* LookupRuleCurrentScope(const std::string& rule_name);
  const std::map<std::string, std::unique_ptr<const Rule>>& GetRules() const;

  void AddBinding(const std::string& key, const std::string& val);

  /// This is tricky.  Edges want lookup scope to go in this order:
  /// 1) value set on edge itself (edge_->env_)
  /// 2) value set on rule, with expansion in the edge's scope
  /// 3) value set on enclosing scope of edge (edge_->env_->parent_)
  /// This function takes as parameters the necessary info to do (2).
  std::string LookupWithFallback(const StringPiece& var, const EvalString* eval,
                                 Env* env);

private:
  std::map<std::string, std::string, StringPieceLess> bindings_;
  std::map<std::string, std::unique_ptr<const Rule>> rules_;
  BindingEnv* parent_;
};

#endif  // NINJA_EVAL_ENV_H_
