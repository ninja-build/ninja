
struct EvalString {
  struct Env {
    virtual string Evaluate(const string& var) = 0;
  };
  bool Parse(const string& input);
  string Evaluate(Env* env);

  const string& unparsed() const { return unparsed_; }

  string unparsed_;
  enum TokenType { RAW, SPECIAL };
  typedef vector<pair<string, TokenType> > TokenList;
  TokenList parsed_;
};

bool EvalString::Parse(const string& input) {
  unparsed_ = input;

  string::size_type start, end;
  start = 0;
  do {
    end = input.find_first_of("@$", start);
    if (end == string::npos) {
      end = input.size();
      break;
    }
    if (end > start)
      parsed_.push_back(make_pair(input.substr(start, end - start), RAW));
    start = end;
    for (end = start + 1; end < input.size(); ++end) {
      char c = input[end];
      if (!(('a' <= c && c <= 'z') || c == '_'))
        break;
    }
    if (end == start + 1) {
      // XXX report bad parse here
      return false;
    }
    parsed_.push_back(make_pair(input.substr(start, end - start), SPECIAL));
    start = end;
  } while (end < input.size());
  if (end > start)
    parsed_.push_back(make_pair(input.substr(start, end - start), RAW));

  return true;
}

string EvalString::Evaluate(Env* env) {
  string result;
  for (TokenList::iterator i = parsed_.begin(); i != parsed_.end(); ++i) {
    if (i->second == RAW)
      result.append(i->first);
    else
      result.append(env->Evaluate(i->first));
  }
  return result;
}

