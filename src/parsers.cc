#include "parsers.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ninja.h"

string Token::AsString() const {
  switch (type_) {
  case IDENT:    return "'" + string(pos_, end_ - pos_) + "'";
  case UNKNOWN:  return "unknown '" + string(pos_, end_ - pos_) + "'";
  case RULE:     return "'rule'";
  case BUILD:    return "'build'";
  case SUBNINJA: return "'subninja'";
  case NEWLINE:  return "newline";
  case EQUALS:   return "'='";
  case COLON:    return "':'";
  case PIPE:     return "'|'";
  case TEOF:     return "eof";
  case INDENT:   return "indenting in";
  case OUTDENT:  return "indenting out";
  case NONE:
  default:
    assert(false);
    return "";
  }
}

void Tokenizer::Start(const char* start, const char* end) {
  cur_line_ = cur_ = start;
  end_ = end;
}

bool Tokenizer::Error(const string& message, string* err) {
  char buf[1024];
  sprintf(buf, "line %d, col %d: %s",
          line_number_,
          (int)(token_.pos_ - cur_line_) + 1,
          message.c_str());
  err->assign(buf);
  return false;
}

void Tokenizer::SkipWhitespace(bool newline) {
  if (token_.type_ == Token::NEWLINE && newline)
    Newline(NULL);

  while (cur_ < end_) {
    if (*cur_ == ' ') {
      ++cur_;
    } else if (newline && *cur_ == '\n') {
      Newline(NULL);
    } else if (*cur_ == '\\' && cur_ + 1 < end_ && cur_[1] == '\n') {
      ++cur_; ++cur_;
      cur_line_ = cur_;
      ++line_number_;
    } else if (*cur_ == '#' && cur_ == cur_line_) {
      while (cur_ < end_ && *cur_ != '\n')
        ++cur_;
      if (cur_ < end_ && *cur_ == '\n') {
        ++cur_;
        cur_line_ = cur_;
        ++line_number_;
      }
    } else {
      break;
    }
  }
}

bool Tokenizer::Newline(string* err) {
  if (!ExpectToken(Token::NEWLINE, err))
    return false;

  return true;
}

static bool IsIdentChar(char c) {
  return
    ('a' <= c && c <= 'z') ||
    ('+' <= c && c <= '9') ||  // +,-./ and numbers
    ('A' <= c && c <= 'Z') ||
    (c == '_') || (c == '$');
}

bool Tokenizer::ExpectToken(Token::Type expected, string* err) {
  PeekToken();
  if (token_.type_ != expected) {
    return Error("expected " + Token(expected).AsString() + ", "
                 "got " + token_.AsString(), err);
  }
  ConsumeToken();
  return true;
}

bool Tokenizer::ReadIdent(string* out) {
  PeekToken();
  if (token_.type_ != Token::IDENT)
    return false;
  out->assign(token_.pos_, token_.end_ - token_.pos_);
  ConsumeToken();
  return true;
}

bool Tokenizer::ReadToNewline(string* text, string* err) {
  // XXX token_.clear();
  while (cur_ < end_ && *cur_ != '\n') {
    if (*cur_ == '\\') {
      ++cur_;
      if (cur_ >= end_)
        return Error("unexpected eof", err);
      if (*cur_ != '\n') {
        // XXX we just let other backslashes through verbatim now.
        // This may not be wise.
        text->push_back('\\');
        text->push_back(*cur_);
        ++cur_;
        continue;
      }
      ++cur_;
      cur_line_ = cur_;
      ++line_number_;
      SkipWhitespace();
      // Collapse whitespace, but make sure we get at least one space.
      if (text->size() > 0 && text->at(text->size() - 1) != ' ')
        text->push_back(' ');
    } else {
      text->push_back(*cur_);
      ++cur_;
    }
  }
  return Newline(err);
}

Token::Type Tokenizer::PeekToken() {
  if (token_.type_ != Token::NONE)
    return token_.type_;

  token_.pos_ = cur_;
  if (cur_indent_ == -1) {
    cur_indent_ = cur_ - cur_line_;
    if (cur_indent_ != last_indent_) {
      if (cur_indent_ > last_indent_) {
        token_.type_ = Token::INDENT;
      } else if (cur_indent_ < last_indent_) {
        token_.type_ = Token::OUTDENT;
      }
      last_indent_ = cur_indent_;
      return token_.type_;
    }
  }

  if (cur_ >= end_) {
    token_.type_ = Token::TEOF;
    return token_.type_;
  }

  if (IsIdentChar(*cur_)) {
    while (cur_ < end_ && IsIdentChar(*cur_)) {
      ++cur_;
    }
    token_.end_ = cur_;
    int len = token_.end_ - token_.pos_;
    if (len == 4 && memcmp(token_.pos_, "rule", 4) == 0)
      token_.type_ = Token::RULE;
    else if (len == 5 && memcmp(token_.pos_, "build", 5) == 0)
      token_.type_ = Token::BUILD;
    else if (len == 8 && memcmp(token_.pos_, "subninja", 8) == 0)
      token_.type_ = Token::SUBNINJA;
    else
      token_.type_ = Token::IDENT;
  } else if (*cur_ == ':') {
    token_.type_ = Token::COLON;
    ++cur_;
  } else if (*cur_ == '=') {
    token_.type_ = Token::EQUALS;
    ++cur_;
  } else if (*cur_ == '|') {
    token_.type_ = Token::PIPE;
    ++cur_;
  } else if (*cur_ == '\n') {
    token_.type_ = Token::NEWLINE;
    ++cur_;
    cur_line_ = cur_;
    cur_indent_ = -1;
    ++line_number_;
  }

  SkipWhitespace();

  if (token_.type_ == Token::NONE) {
    token_.type_ = Token::UNKNOWN;
    token_.end_ = cur_ + 1;
  }

  return token_.type_;
}

void Tokenizer::ConsumeToken() {
  token_.Clear();
}

bool MakefileParser::Parse(const string& input, string* err) {
  tokenizer_.Start(input.data(), input.data() + input.size());

  if (!tokenizer_.ReadIdent(&out_))
    return tokenizer_.Error("expected output filename", err);
  if (!tokenizer_.ExpectToken(Token::COLON, err))
    return false;
  while (tokenizer_.PeekToken() == Token::IDENT) {
    string in;
    tokenizer_.ReadIdent(&in);
    ins_.push_back(in);
  }
  if (!tokenizer_.ExpectToken(Token::NEWLINE, err))
    return false;
  if (!tokenizer_.ExpectToken(Token::TEOF, err))
    return false;

  return true;
}

ManifestParser::ManifestParser(State* state, FileReader* file_reader)
  : state_(state), file_reader_(file_reader) {
  env_ = &state->bindings_;
}
bool ManifestParser::Load(const string& filename, string* err) {
  string contents;
  if (!file_reader_->ReadFile(filename, &contents, err))
    return false;
  return Parse(contents, err);
}

bool ManifestParser::Parse(const string& input, string* err) {
  tokenizer_.Start(input.data(), input.data() + input.size());

  tokenizer_.SkipWhitespace(true);

  while (tokenizer_.token().type_ != Token::TEOF) {
    switch (tokenizer_.PeekToken()) {
      case Token::RULE:
        if (!ParseRule(err))
          return false;
        break;
      case Token::BUILD:
        if (!ParseEdge(err))
          return false;
        break;
      case Token::SUBNINJA:
        if (!ParseSubNinja(err))
          return false;
        break;
      case Token::IDENT: {
        string name, value;
        if (!ParseLet(&name, &value, true, err))
          return false;
        if (value.substr(0, 9) == "ROOT_HACK") {
          // XXX remove this hack, or make it more principled.
          char cwd[1024];
          if (!getcwd(cwd, sizeof(cwd))) {
            perror("getcwd");
            return 1;
          }
          value = cwd + value.substr(9);
        }
        env_->AddBinding(name, value);
        break;
      }
      case Token::TEOF:
        continue;
      default:
        return tokenizer_.Error("unhandled " + tokenizer_.token().AsString(), err);
    }
    tokenizer_.SkipWhitespace(true);
  }

  return true;
}

bool ManifestParser::ParseRule(string* err) {
  if (!tokenizer_.ExpectToken(Token::RULE, err))
    return false;
  string name;
  if (!tokenizer_.ReadIdent(&name)) {
    return tokenizer_.Error("expected rule name, got " + tokenizer_.token().AsString(),
                         err);
  }
  if (!tokenizer_.Newline(err))
    return false;

  if (state_->LookupRule(name) != NULL) {
    *err = "duplicate rule '" + name + "'";
    return false;
  }

  Rule* rule = new Rule(name);  // XXX scoped_ptr

  if (tokenizer_.PeekToken() == Token::INDENT) {
    tokenizer_.ConsumeToken();

    while (tokenizer_.PeekToken() != Token::OUTDENT) {
      string key, val;
      if (!ParseLet(&key, &val, false, err))
        return false;

      string parse_err;
      if (key == "command") {
        if (!rule->ParseCommand(val, &parse_err))
          return tokenizer_.Error(parse_err, err);
      } else if (key == "depfile") {
        if (!rule->depfile_.Parse(val, &parse_err))
          return tokenizer_.Error(parse_err, err);
      } else if (key == "description") {
        if (!rule->description_.Parse(val, &parse_err))
          return tokenizer_.Error(parse_err, err);
      } else {
        // Die on other keyvals for now; revisit if we want to add a
        // scope here.
        return tokenizer_.Error("unexpected variable '" + key + "'", err);
      }
    }
    tokenizer_.ConsumeToken();
  }

  if (rule->command_.unparsed().empty())
    return tokenizer_.Error("expected 'command =' line", err);

  state_->AddRule(rule);
  return true;
}

bool ManifestParser::ParseLet(string* name, string* value, bool expand,
                              string* err) {
  if (!tokenizer_.ReadIdent(name))
    return tokenizer_.Error("expected variable name", err);
  if (!tokenizer_.ExpectToken(Token::EQUALS, err))
    return false;

  // XXX should we tokenize here?  it means we'll need to understand
  // command syntax, though...
  if (!tokenizer_.ReadToNewline(value, err))
    return false;

  if (expand) {
    EvalString eval;
    string eval_err;
    if (!eval.Parse(*value, &eval_err))
      return tokenizer_.Error(eval_err, err);
    *value = eval.Evaluate(env_);
  }

  return true;
}

static string CanonicalizePath(const string& path) {
  string out;
  for (size_t i = 0; i < path.size(); ++i) {
    char in = path[i];
    if (in == '/' &&
        (!out.empty() && *out.rbegin() == '/')) {
      continue;
    }
    out.push_back(in);
  }
  return out;
}

bool ManifestParser::ParseEdge(string* err) {
  vector<string> ins, outs;

  if (!tokenizer_.ExpectToken(Token::BUILD, err))
    return false;

  for (;;) {
    if (tokenizer_.PeekToken() == Token::COLON) {
      tokenizer_.ConsumeToken();
      break;
    }

    string out;
    if (!tokenizer_.ReadIdent(&out))
      return tokenizer_.Error("expected output file list", err);
    outs.push_back(out);
  }
  // XXX check outs not empty

  string rule_name;
  if (!tokenizer_.ReadIdent(&rule_name))
    return tokenizer_.Error("expected build command name", err);

  const Rule* rule = state_->LookupRule(rule_name);
  if (!rule)
    return tokenizer_.Error("unknown build rule '" + rule_name + "'", err);

  if (!rule->depfile_.empty()) {
    if (outs.size() > 1) {
      return tokenizer_.Error("dependency files only work with single-output "
                           "rules", err);
    }
  }

  for (;;) {
    string in;
    if (!tokenizer_.ReadIdent(&in))
      break;
    ins.push_back(in);
  }

  // Add all order-only deps, counting how many as we go.
  int order_only = 0;
  if (tokenizer_.PeekToken() == Token::PIPE) {
    tokenizer_.ConsumeToken();
    for (;;) {
      string in;
      if (!tokenizer_.ReadIdent(&in))
        break;
      ins.push_back(in);
      ++order_only;
    }
  }

  if (!tokenizer_.Newline(err))
    return false;

  // Default to using outer env.
  BindingEnv* env = env_;

  // But use a nested env if there are variables in scope.
  if (tokenizer_.PeekToken() == Token::INDENT) {
    tokenizer_.ConsumeToken();

    // XXX scoped_ptr to handle error case.
    env = new BindingEnv;
    env->parent_ = env_;
    while (tokenizer_.PeekToken() != Token::OUTDENT) {
      string key, val;
      if (!ParseLet(&key, &val, false, err))
        return false;
      env->AddBinding(key, val);
    }
    tokenizer_.ConsumeToken();
  }

  // Evaluate all variables in paths.
  // XXX: fast path skip the eval parse if there's no $ in the path?
  vector<string>* paths[2] = { &ins, &outs };
  for (int p = 0; p < 2; ++p) {
    for (vector<string>::iterator i = paths[p]->begin();
         i != paths[p]->end(); ++i) {
      EvalString eval;
      string eval_err;
      if (!eval.Parse(*i, &eval_err))
        return tokenizer_.Error(eval_err, err);
      *i = CanonicalizePath(eval.Evaluate(env));
    }
  }

  Edge* edge = state_->AddEdge(rule);
  edge->env_ = env;
  for (vector<string>::iterator i = ins.begin(); i != ins.end(); ++i)
    state_->AddInOut(edge, Edge::IN, *i);
  for (vector<string>::iterator i = outs.begin(); i != outs.end(); ++i)
    state_->AddInOut(edge, Edge::OUT, *i);
  edge->order_only_deps_ = order_only;

  return true;
}

bool ManifestParser::ParseSubNinja(string* err) {
  if (!tokenizer_.ExpectToken(Token::SUBNINJA, err))
    return false;
  string path;
  if (!tokenizer_.ReadIdent(&path))
    return tokenizer_.Error("expected subninja path", err);
  if (!tokenizer_.Newline(err))
    return false;

  string contents;
  if (!file_reader_->ReadFile(path, &contents, err))
    return false;

  ManifestParser subparser(state_, file_reader_);
  // Simulate variable inheritance of builddir.
  subparser.builddir_ = builddir_;
  subparser.env_ = new BindingEnv;
  subparser.env_->parent_ = env_;
  string sub_err;
  if (!subparser.Parse(contents, &sub_err))
    return tokenizer_.Error("in '" + path + "': " + sub_err, err);

  return true;
}
