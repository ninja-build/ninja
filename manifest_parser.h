#include <errno.h>
#include <stdio.h>
#include <string.h>

struct Parser {
  Parser() : line_number_(1) {}

  void Start(const char* start, const char* end);
  bool Error(const string& message, string* err);

  const string& token() const { return token_; }
  bool eof() const { return cur_ >= end_; }

  void SkipWhitespace(bool newline=false);
  bool Newline(string* err);
  bool Token(const string& expected, string* err);
  bool ReadToken(string* out);
  bool ReadToNewline(string* text, string* err);

  bool PeekToken();
  void AdvanceToken();

  const char* cur_line_;
  const char* cur_;
  const char* end_;
  int line_number_;
  string token_;
  const char* err_start_;
};

void Parser::Start(const char* start, const char* end) {
  cur_line_ = cur_ = start;
  end_ = end;
}

bool Parser::Error(const string& message, string* err) {
  char buf[1024];
  sprintf(buf, "line %d, col %d: %s",
          line_number_,
          (int)(err_start_ - cur_line_) + 1,
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
    } else {
      break;
    }
  }
}

bool Parser::Newline(string* err) {
  err_start_ = cur_;
  if (cur_ < end_ && *cur_ == '\n') {
    ++cur_;
    cur_line_ = cur_;
    ++line_number_;
    token_.clear();
    return true;
  }

  if (err) {
    if (cur_ >= end_)
      return Error("expected newline, got eof", err);
    else
      return Error(string("expected newline, got '") + *cur_ + string("'"), err);
  }
  return false;
}

static bool IsIdentChar(char c) {
  return
    ('a' <= c && c <= 'z') ||
    ('+' <= c && c <= '9') ||  // +,-./ and numbers
    ('A' <= c && c <= 'Z') ||
    (c == '_');
}

bool Parser::Token(const string& expected, string* err) {
  PeekToken();
  if (token_ != expected)
    return Error("expected '" + expected + "', got '" + token_ + "'", err);
  AdvanceToken();
  return true;
}

bool Parser::ReadToken(string* out) {
  if (!PeekToken())
    return false;
  out->assign(token_);
  AdvanceToken();
  return true;
}

bool Parser::ReadToNewline(string* text, string* err) {
  token_.clear();
  while (cur_ < end_ && *cur_ != '\n') {
    if (*cur_ == '\\') {
      ++cur_;
      if (cur_ >= end_)
        return Error("unexpected eof", err);
      if (!Newline(err))
        return false;
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

bool Parser::PeekToken() {
  if (!token_.empty())
    return true;

  err_start_ = cur_;

  if (cur_ >= end_)
    return false;

  if (IsIdentChar(*cur_)) {
    while (cur_ < end_ && IsIdentChar(*cur_)) {
      token_.push_back(*cur_);
      ++cur_;
    }
  } else if (*cur_ == ':' || *cur_ == '=') {
    token_ = *cur_;
    ++cur_;
  }

  SkipWhitespace();

  return !token_.empty();
}

void Parser::AdvanceToken() {
  token_.clear();
}

struct ManifestParser {
  ManifestParser(State* state) : state_(state) {}
  bool Load(const string& filename, string* err);
  bool Parse(const string& input, string* err);

  bool ParseRule(string* err);
  bool ParseLet(string* err);
  bool ParseEdge(string* err);

  State* state_;
  Parser parser_;
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

  while (parser_.PeekToken()) {
    if (parser_.token() == "rule") {
      if (!ParseRule(err))
        return false;
    } else if (parser_.token() == "build") {
      if (!ParseEdge(err))
        return false;
    } else {
      if (!ParseLet(err))
        return false;
    }
    parser_.SkipWhitespace(true);
  }

  if (!parser_.eof())
    return parser_.Error("expected eof", err);

  return true;
}

bool ManifestParser::ParseRule(string* err) {
  if (!parser_.Token("rule", err))
    return false;
  string name;
  if (!parser_.ReadToken(&name))
    return parser_.Error("expected rule name", err);
  if (!parser_.Newline(err))
    return false;

  if (!parser_.Token("command", err))
    return false;
  string command;
  if (!parser_.ReadToNewline(&command, err))
    return false;

  state_->AddRule(name, command);

  return true;
}

bool ManifestParser::ParseLet(string* err) {
  string name;
  if (!parser_.ReadToken(&name))
    return parser_.Error("expected variable name", err);

  if (!parser_.Token("=", err))
    return false;

  string value;
  if (!parser_.ReadToNewline(&value, err))
    return false;

  state_->AddBinding(name, value);

  return true;
}

bool ManifestParser::ParseEdge(string* err) {
  vector<string> ins, outs;

  if (!parser_.Token("build", err))
    return false;

  for (;;) {
    string out;
    if (!parser_.ReadToken(&out))
      return parser_.Error("expected output file list", err);
    if (out == ":")
      break;
    outs.push_back(out);
  }

  string rule;
  if (!parser_.ReadToken(&rule))
    return parser_.Error("expected build command name", err);

  for (;;) {
    string in;
    if (!parser_.ReadToken(&in))
      break;
    ins.push_back(in);
  }

  if (!parser_.Newline(err))
    return false;

  Edge* edge = state_->AddEdge(rule);
  if (!edge)
    return parser_.Error("unknown build rule name name", err);
  for (vector<string>::iterator i = ins.begin(); i != ins.end(); ++i)
    state_->AddInOut(edge, Edge::IN, *i);
  for (vector<string>::iterator i = outs.begin(); i != outs.end(); ++i)
    state_->AddInOut(edge, Edge::OUT, *i);

  return true;
}

