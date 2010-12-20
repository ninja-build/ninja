#ifndef NINJA_EVAL_ENV_H_
#define NINJA_EVAL_ENV_H_

#include <map>
using namespace std;

// A scope for variable lookups.
struct Env {
  virtual string LookupVariable(const string& var) = 0;
};

// A standard scope, which contains a mapping of variables to values
// as well as a pointer to a parent scope.
struct BindingEnv : public Env {
  BindingEnv() : parent_(NULL) {}
  virtual string LookupVariable(const string& var) {
    map<string, string>::iterator i = bindings_.find(var);
    if (i != bindings_.end())
      return i->second;
    if (parent_)
      return parent_->LookupVariable(var);
    return "";
  }
  void AddBinding(const string& key, const string& val) {
    bindings_[key] = val;
  }

  map<string, string> bindings_;
  Env* parent_;
};

// A tokenized string that contains variable references.
// Can be evaluated relative to an Env.
struct EvalString {
  bool Parse(const string& input, string* err);
  string Evaluate(Env* env) const;

  const string& unparsed() const { return unparsed_; }
  const bool empty() const { return unparsed_.empty(); }

  string unparsed_;
  enum TokenType { RAW, SPECIAL };
  typedef vector<pair<string, TokenType> > TokenList;
  TokenList parsed_;
};

#endif  // NINJA_EVAL_ENV_H_
