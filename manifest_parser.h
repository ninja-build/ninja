#include <errno.h>
#include <stdio.h>
#include <string.h>

struct Token {
  enum Type {
    NONE,
    IDENT,
    RULE,
    BUILD,
    NEWLINE,
    EQUALS,
    COLON,
    INDENT,
    OUTDENT,
    TEOF
  };
  explicit Token(Type type) : type_(type) {}

  void Clear() { type_ = NONE; extra_.clear(); }
  string AsString() const {
    switch (type_) {
      case IDENT:   return "'" + extra_ + "'";
      case RULE:    return "'rule'";
      case BUILD:   return "'build'";
      case NEWLINE: return "newline";
      case EQUALS:  return "'='";
      case COLON:   return "':'";
      case TEOF:    return "eof";
      case INDENT:  return "indenting in";
      case OUTDENT: return "indenting out";
      case NONE:
      default:
        assert(false);
        return "";
    }
  }

  Type type_;
  const char* pos_;
  string extra_;
};

struct Parser {
  Parser()
      : token_(Token::NONE), line_number_(1),
        last_indent_(0), cur_indent_(-1) {}

  void Start(const char* start, const char* end);
  bool Error(const string& message, string* err);

  const Token& token() const { return token_; }

  void SkipWhitespace(bool newline=false);
  bool Newline(string* err);
  bool ExpectToken(Token::Type expected, string* err);
  bool ReadIdent(string* out);
  bool ReadToNewline(string* text, string* err);

  Token::Type PeekToken();
  void ConsumeToken();

  const char* cur_;
  const char* end_;

  const char* cur_line_;
  Token token_;
  int line_number_;
  int last_indent_, cur_indent_;
};

void Parser::Start(const char* start, const char* end) {
  cur_line_ = cur_ = start;
  end_ = end;
}

bool Parser::Error(const string& message, string* err) {
  char buf[1024];
  sprintf(buf, "line %d, col %d: %s",
          line_number_,
          (int)(token_.pos_ - cur_line_) + 1,
          message.c_str());
  err->assign(buf);
  return false;
}

void Parser::SkipWhitespace(bool newline) {
  while (cur_ < end_) {
    if (*cur_ == ' ') {
      ++cur_;
    } else if (newline && *cur_ == '\n') {
      Newline(NULL);
    } else if (*cur_ == '\\' && cur_ + 1 < end_ && cur_[1] == '\n') {
      ++cur_; ++cur_;
      cur_line_ = cur_;
      ++line_number_;
    } else {
      break;
    }
  }
}

bool Parser::Newline(string* err) {
  if (!ExpectToken(Token::NEWLINE, err))
    return false;

  return true;
}

static bool IsIdentChar(char c) {
  return
    ('a' <= c && c <= 'z') ||
    ('+' <= c && c <= '9') ||  // +,-./ and numbers
    ('A' <= c && c <= 'Z') ||
    (c == '_') || (c == '@');
}

bool Parser::ExpectToken(Token::Type expected, string* err) {
  PeekToken();
  if (token_.type_ != expected) {
    return Error("expected " + Token(expected).AsString() + ", "
                 "got " + token_.AsString(), err);
  }
  ConsumeToken();
  return true;
}

bool Parser::ReadIdent(string* out) {
  PeekToken();
  if (token_.type_ != Token::IDENT)
    return false;
  out->assign(token_.extra_);
  ConsumeToken();
  return true;
}

bool Parser::ReadToNewline(string* text, string* err) {
  // XXX token_.clear();
  while (cur_ < end_ && *cur_ != '\n') {
    if (*cur_ == '\\') {
      ++cur_;
      if (cur_ >= end_)
        return Error("unexpected eof", err);
      if (*cur_ != '\n')
        return Error("expected newline after backslash", err);
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

Token::Type Parser::PeekToken() {
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
      token_.extra_.push_back(*cur_);
      ++cur_;
    }
    if (token_.extra_ == "rule")
      token_.type_ = Token::RULE;
    else if (token_.extra_ == "build")
      token_.type_ = Token::BUILD;
    else
      token_.type_ = Token::IDENT;
  } else if (*cur_ == ':') {
    token_.type_ = Token::COLON;
    ++cur_;
  } else if (*cur_ == '=') {
    token_.type_ = Token::EQUALS;
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
    assert(false); // XXX
  }
  return token_.type_;
}

void Parser::ConsumeToken() {
  token_.Clear();
}

struct ManifestParser {
  ManifestParser(State* state) : state_(state) {}
  bool Load(const string& filename, string* err);
  bool Parse(const string& input, string* err);

  bool ParseRule(string* err);
  bool ParseLet(string* key, string* val, string* err);
  bool ParseEdge(string* err);

  string ExpandFile(const string& file);

  State* state_;
  Parser parser_;
  string builddir_;
};

bool ManifestParser::Load(const string& filename, string* err) {
  FILE* f = fopen(filename.c_str(), "r");
  if (!f) {
    err->assign(strerror(errno));
    return false;
  }

  string text;
  char buf[64 << 10];
  size_t len;
  while ((len = fread(buf, 1, sizeof(buf), f)) > 0) {
    text.append(buf, len);
  }
  if (ferror(f)) {
    err->assign(strerror(errno));
    fclose(f);
    return false;
  }
  fclose(f);

  return Parse(text, err);
}

bool ManifestParser::Parse(const string& input, string* err) {
  parser_.Start(input.data(), input.data() + input.size());

  parser_.SkipWhitespace(true);

  while (parser_.token().type_ != Token::TEOF) {
    switch (parser_.PeekToken()) {
      case Token::RULE:
        if (!ParseRule(err))
          return false;
        break;
      case Token::BUILD:
        if (!ParseEdge(err))
          return false;
        break;
      case Token::IDENT: {
        string name, value;
        if (!ParseLet(&name, &value, err))
          return false;

        state_->AddBinding(name, value);
        if (name == "builddir") {
          builddir_ = value;
          if (!builddir_.empty() && builddir_[builddir_.size() - 1] != '/')
            builddir_.push_back('/');
        }
        break;
      }
      case Token::TEOF:
        continue;
      default:
        return parser_.Error("unhandled " + parser_.token().AsString(), err);
    }
    parser_.SkipWhitespace(true);
  }

  return true;
}

bool ManifestParser::ParseRule(string* err) {
  if (!parser_.ExpectToken(Token::RULE, err))
    return false;
  string name;
  if (!parser_.ReadIdent(&name))
    return parser_.Error("expected rule name", err);
  if (!parser_.Newline(err))
    return false;

  string command;
  if (parser_.PeekToken() == Token::INDENT) {
    parser_.ConsumeToken();

    while (parser_.PeekToken() != Token::OUTDENT) {
      string key, val;
      if (!ParseLet(&key, &val, err))
        return false;

      if (key == "command")
        command = val;
    }
    parser_.ConsumeToken();
  }

  if (command.empty())
    return parser_.Error("expected 'command =' line", err);

  state_->AddRule(name, command);
  return true;
}

bool ManifestParser::ParseLet(string* name, string* value, string* err) {
  if (!parser_.ReadIdent(name))
    return parser_.Error("expected variable name", err);
  if (!parser_.ExpectToken(Token::EQUALS, err))
    return false;
  if (!parser_.ReadToNewline(value, err))
    return false;
  return true;
}

bool ManifestParser::ParseEdge(string* err) {
  vector<string> ins, outs;

  if (!parser_.ExpectToken(Token::BUILD, err))
    return false;

  for (;;) {
    if (parser_.PeekToken() == Token::COLON) {
      parser_.ConsumeToken();
      break;
    }

    string out;
    if (!parser_.ReadIdent(&out))
      return parser_.Error("expected output file list", err);
    outs.push_back(ExpandFile(out));
  }
  // XXX check outs not empty

  string rule_name;
  if (!parser_.ReadIdent(&rule_name))
    return parser_.Error("expected build command name", err);

  Rule* rule = state_->LookupRule(rule_name);
  if (!rule)
    return parser_.Error("unknown build rule '" + rule_name + "'", err);

  for (;;) {
    string in;
    if (!parser_.ReadIdent(&in))
      break;
    ins.push_back(ExpandFile(in));
  }

  if (!parser_.Newline(err))
    return false;

  Edge* edge = state_->AddEdge(rule);
  for (vector<string>::iterator i = ins.begin(); i != ins.end(); ++i)
    state_->AddInOut(edge, Edge::IN, *i);
  for (vector<string>::iterator i = outs.begin(); i != outs.end(); ++i)
    state_->AddInOut(edge, Edge::OUT, *i);

  return true;
}

string ManifestParser::ExpandFile(const string& file) {
  if (!file.empty() && file[0] == '@')
    return builddir_ + file.substr(1);
  return file;
}

