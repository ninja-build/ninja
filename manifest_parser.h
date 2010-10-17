#include <errno.h>
#include <stdio.h>
#include <string.h>

struct ManifestParser {
  ManifestParser(State* state) : state_(state), line_(0), col_(0) {}
  bool Load(const string& filename, string* err);
  bool Parse(const string& input, string* err);

  bool Error(const string& message, string* err);

  bool ParseRule(string* err);
  bool ParseEdge(string* err);

  bool SkipWhitespace(bool newline=false);
  bool Newline(string* err);
  bool NextToken();
  bool ReadToNewline(string* text, string* err);

  State* state_;
  const char* cur_;
  const char* end_;
  int line_, col_;
  string token_;
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
  cur_ = input.data(); end_ = cur_ + input.size();
  line_ = col_ = 0;

  while (NextToken()) {
    if (token_ == "rule") {
      if (!ParseRule(err))
        return false;
    } else if (token_ == "build") {
      if (!ParseEdge(err))
        return false;
    } else {
      return Error("unknown token: " + token_, err);
    }
    SkipWhitespace(true);
  }

  if (cur_ < end_)
    return Error("expected eof", err);

  return true;
}

bool ManifestParser::Error(const string& message, string* err) {
  char buf[1024];
  sprintf(buf, "line %d, col %d: %s", line_ + 1, col_, message.c_str());
  err->assign(buf);
  return false;
}

bool ManifestParser::ParseRule(string* err) {
  SkipWhitespace();
  if (!NextToken())
    return Error("expected rule name", err);
  if (!Newline(err))
    return false;
  string name = token_;

  if (!NextToken() || token_ != "command")
    return Error("expected command", err);
  string command;
  SkipWhitespace();
  if (!ReadToNewline(&command, err))
    return false;

  state_->AddRule(name, command);

  return true;
}

bool ManifestParser::ParseEdge(string* err) {
  string rule;
  vector<string> ins, outs;
  SkipWhitespace();
  for (;;) {
    if (!NextToken())
      return Error("expected output file list", err);
    if (token_ == ":")
      break;
    outs.push_back(token_);
  }
  if (!NextToken())
    return Error("expected build command name", err);
  rule = token_;
  for (;;) {
    if (!NextToken())
      break;
    ins.push_back(token_);
  }
  if (!Newline(err))
    return false;

  Edge* edge = state_->AddEdge(rule);
  if (!edge)
    return Error("unknown build rule name name", err);
  for (vector<string>::iterator i = ins.begin(); i != ins.end(); ++i)
    state_->AddInOut(edge, Edge::IN, *i);
  for (vector<string>::iterator i = outs.begin(); i != outs.end(); ++i)
    state_->AddInOut(edge, Edge::OUT, *i);

  return true;
}

bool ManifestParser::SkipWhitespace(bool newline) {
  bool skipped = false;
  while (cur_ < end_) {
    if (*cur_ == ' ') {
      ++col_;
    } else if (newline && *cur_ == '\n') {
      col_ = 0; ++line_;
    } else {
      break;
    }
    skipped = true;
    ++cur_;
  }
  return skipped;
}

bool ManifestParser::Newline(string* err) {
  if (cur_ < end_ && *cur_ == '\n') {
    ++cur_; ++line_; col_ = 0;
    return true;
  } else {
    if (cur_ >= end_)
      return Error("expected newline, got eof", err);
    else
      return Error(string("expected newline, got '") + *cur_ + string("'"), err);
  }
}

static bool IsIdentChar(char c) {
  return
    ('a' <= c && c <= 'z') ||
    ('+' <= c && c <= '9') ||  // +,-./ and numbers
    ('A' <= c && c <= 'Z') ||
    (c == '_');
}

bool ManifestParser::NextToken() {
  SkipWhitespace();
  token_.clear();
  if (cur_ >= end_)
    return false;

  if (IsIdentChar(*cur_)) {
    while (cur_ < end_ && IsIdentChar(*cur_)) {
      token_.push_back(*cur_);
      ++col_; ++cur_;
    }
  } else if (*cur_ == ':') {
    token_ = ":";
    ++col_; ++cur_;
  }

  return !token_.empty();
}

bool ManifestParser::ReadToNewline(string* text, string* err) {
  while (cur_ < end_ && *cur_ != '\n') {
    text->push_back(*cur_);
    ++cur_; ++col_;
  }
  return Newline(err);
}
