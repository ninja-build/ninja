#include "eval_env.h"

string BindingEnv::LookupVariable(const string& var) {
  map<string, string>::iterator i = bindings_.find(var);
  if (i != bindings_.end())
    return i->second;
  if (parent_)
    return parent_->LookupVariable(var);
  return "";
}

void BindingEnv::AddBinding(const string& key, const string& val) {
  bindings_[key] = val;
}

bool EvalString::Parse(const string& input, string* err) {
  unparsed_ = input;

  string::size_type start, end;
  start = 0;
  do {
    end = input.find('$', start);
    if (end == string::npos) {
      end = input.size();
      break;
    }
    if (end > start)
      parsed_.push_back(make_pair(input.substr(start, end - start), RAW));
    start = end + 1;
    if (start < input.size() && input[start] == '{') {
      ++start;
      for (end = start + 1; end < input.size(); ++end) {
        if (input[end] == '}')
          break;
      }
      if (end >= input.size()) {
        *err = "expected closing curly after ${";
        return false;
      }
      parsed_.push_back(make_pair(input.substr(start, end - start), SPECIAL));
      ++end;
    } else {
      for (end = start; end < input.size(); ++end) {
        char c = input[end];
        if (!(('a' <= c && c <= 'z') || ('0' <= c && c <= '9') || c == '_'))
          break;
      }
      if (end == start) {
        *err = "expected variable after $";
        return false;
      }
      parsed_.push_back(make_pair(input.substr(start, end - start), SPECIAL));
    }
    start = end;
  } while (end < input.size());
  if (end > start)
    parsed_.push_back(make_pair(input.substr(start, end - start), RAW));

  return true;
}

string EvalString::Evaluate(Env* env) const {
  string result;
  for (TokenList::const_iterator i = parsed_.begin(); i != parsed_.end(); ++i) {
    if (i->second == RAW)
      result.append(i->first);
    else
      result.append(env->LookupVariable(i->first));
  }
  return result;
}
