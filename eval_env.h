
struct EvalString {
  struct Env {
    virtual string Evaluate(const string& var) = 0;
  };
  bool Parse(const string& input, string* err);
  string Evaluate(Env* env) const;

  const string& unparsed() const { return unparsed_; }
  const bool empty() const { return unparsed_.empty(); }

  string unparsed_;
  enum TokenType { RAW, SPECIAL };
  typedef vector<pair<string, TokenType> > TokenList;
  TokenList parsed_;
};
