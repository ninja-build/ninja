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
#include <string>
#include <vector>
using namespace std;

#include "string_piece.h"

struct Rule;

/// An interface for a scope for variable (e.g. "$foo") lookups.
struct Env {
  virtual ~Env() {}
  virtual string LookupVariable(const string& var) = 0;
};

/// A tokenized string that contains variable references.
/// Can be evaluated relative to an Env.
struct EvalString {
  /// @return The evaluated string with variable expanded using value found in
  ///         environment @a env.
  string Evaluate(Env* env) const;

  /// @return The string with variables not expanded.
  string Unparse() const;

  void Clear() { parsed_.clear(); }
  bool empty() const { return parsed_.empty(); }

  void AddText(StringPiece text);
  void AddSpecial(StringPiece text);

  /// Construct a human-readable representation of the parsed state,
  /// for use in tests.
  string Serialize() const;

private:
  enum TokenType { RAW, SPECIAL };
  typedef vector<pair<string, TokenType> > TokenList;
  TokenList parsed_;
};

/// An invokable build command and associated metadata (description, etc.).
struct Rule {
  explicit Rule(const string& name) : name_(name) {}

  const string& name() const { return name_; }

  void AddBinding(const string& key, const EvalString& val);

  static bool IsReservedBinding(const string& var);

  const EvalString* GetBinding(const string& key) const;

 private:
  // Allow the parsers to reach into this object and fill out its fields.
  friend struct ManifestParser;

  string name_;
  typedef map<string, EvalString> Bindings;
  Bindings bindings_;
};

struct RelPathEnv : public Env {
  RelPathEnv(const string& rel_path, const string& abs_path)
    : rel_path_(rel_path),
      abs_path_(abs_path) {}

  const string& AsString() const { return abs_path_; }
  bool equals(RelPathEnv* other) { return other->abs_path_ == abs_path_; }
  bool HasRelPath() const { return !rel_path_.empty(); }
  string ApplyChdir(string s) const {
    if (s.size() > 0 && s[0] == '/') {
      return s;
    }
    // Optimization: only simplify ./ and ../ on boundary between a and s.
    // To process all ./ and ../, wrap the following code in a loop. At the end
    // of the loop, search for ./ or ../ and construct new a and s using that as
    // the new boundary.
    //
    // Note: there are dangerous filesystem bugs in assuming that ../ can be
    // simplified in this way. It only "works" when the dir before ../ in the
    // relative path is a physical directory entry (so, no symlinks, FS joins,
    // or other filesystem features).
    string a = AsString();
    while (!a.empty() && s.size() > 3 &&
        s[0] == '.') {
      if (s[1] == '/') {
        s.erase(0, 2);  // Remove "./" from s.
      } else if (s[1] == '.' && s[2] == '/') {
        string::size_type i = a.rfind('/', a.size() - 2);
        if (i == string::npos) {
          a.clear();
        } else {
          i++;
          a.erase(i, a.size() - i);  // Remove "../" from s and one dir from a.
        }
        s.erase(0, 3);
      }
    }
    return a + s;
  }

private:
  // rel_path_ is relative to the parent BindingEnv.
  string rel_path_;
  // abs_path_ is still relative to the root ninja invocation.
  string abs_path_;
};

/// An Env which contains a mapping of variables to values
/// as well as a pointer to a parent scope.
struct BindingEnv : public RelPathEnv {
  BindingEnv() : RelPathEnv("", ""), parent_(NULL) {}
  explicit BindingEnv(BindingEnv* parent)
    : RelPathEnv("", parent->AsString()), parent_(parent) {}
  explicit BindingEnv(BindingEnv* parent, const string& rel_path,
                      const string& abs_path)
    : RelPathEnv(rel_path, abs_path), parent_(parent) {}

  virtual ~BindingEnv() {}
  virtual string LookupVariable(const string& var);

  void AddRule(const Rule* rule);
  const Rule* LookupRule(const string& rule_name);
  const Rule* LookupRuleCurrentScope(const string& rule_name);
  const map<string, const Rule*>& GetRules() const;

  void AddBinding(const string& key, const string& val);

  /// This is tricky.  Edges want lookup scope to go in this order:
  /// 1) value set on edge itself (edge_->env_)
  /// 2) value set on rule, with expansion in the edge's scope
  /// 3) value set on enclosing scope of edge (edge_->env_->parent_)
  /// This function takes as parameters the necessary info to do (2).
  string LookupWithFallback(const string& var, const EvalString* eval,
                            Env* env);

private:
  map<string, string> bindings_;
  map<string, const Rule*> rules_;
  BindingEnv* parent_;
};

#endif  // NINJA_EVAL_ENV_H_
