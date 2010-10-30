#include "parsers.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ninja.h"

string Token::AsString() const {
  switch (type_) {
  case IDENT:   return "'" + extra_ + "'";
  case UNKNOWN: return "unknown '" + extra_ + "'";
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
    (c == '_') || (c == '@');
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
  out->assign(token_.extra_);
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
    token_.type_ = Token::UNKNOWN;
    token_.extra_ = *cur_;
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

// XXX refactor.
extern string ReadFile(const string& path, string* err);

bool ManifestParser::Load(const string& filename, string* err) {
  string input = ReadFile(filename, err);
  if (!err->empty())
    return false;
  return Parse(input, err);
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
      if (!ParseLet(&key, &val, err))
        return false;

      if (key == "command") {
        rule->ParseCommand(val);
      } else if (key == "depfile") {
        rule->depfile_.Parse(val);
      }
    }
    tokenizer_.ConsumeToken();
  }

  if (rule->command_.unparsed().empty())
    return tokenizer_.Error("expected 'command =' line", err);

  state_->AddRule(rule);
  return true;
}

bool ManifestParser::ParseLet(string* name, string* value, string* err) {
  if (!tokenizer_.ReadIdent(name))
    return tokenizer_.Error("expected variable name", err);
  if (!tokenizer_.ExpectToken(Token::EQUALS, err))
    return false;
  if (!tokenizer_.ReadToNewline(value, err))
    return false;
  return true;
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
    outs.push_back(ExpandFile(out));
  }
  // XXX check outs not empty

  string rule_name;
  if (!tokenizer_.ReadIdent(&rule_name))
    return tokenizer_.Error("expected build command name", err);

  Rule* rule = state_->LookupRule(rule_name);
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
    ins.push_back(ExpandFile(in));
  }

  if (!tokenizer_.Newline(err))
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

