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

#include "parsers.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "graph.h"
#include "ninja.h"
#include "util.h"

string Token::AsString() const {
  switch (type_) {
  case IDENT:    return "'" + string(pos_, end_ - pos_) + "'";
  case UNKNOWN:  return "unknown '" + string(pos_, end_ - pos_) + "'";
  case NEWLINE:  return "newline";
  case EQUALS:   return "'='";
  case COLON:    return "':'";
  case PIPE:     return "'|'";
  case PIPE2:    return "'||'";
  case TEOF:     return "eof";
  case INDENT:   return "indenting in";
  case OUTDENT:  return "indenting out";
  case NONE:     break;
  }
  assert(false);
  return "";
}

bool Tokenizer::ErrorAt(const char* pos, const string& message, string* err) {
  // Re-scan the input, counting newlines so that we can compute the
  // correct position.
  int line = 1;
  const char* line_start = start_;
  for (const char* p = start_; p < pos; ++p) {
    if (*p == '\n') {
      ++line;
      line_start = p + 1;
    }
  }
  int col = pos - line_start + 1;

  char buf[1024];
  snprintf(buf, sizeof(buf),
           "line %d, col %d: %s", line, col, message.c_str());
  err->assign(buf);
  return false;
}

void Tokenizer::Start(const char* start, const char* end) {
  cur_line_ = cur_ = start_ = start;
  end_ = end;
}

bool Tokenizer::ErrorExpected(const string& expected, string* err) {
  return Error("expected " + expected + ", got " + token_.AsString(), err);
}

void Tokenizer::SkipWhitespace(bool newline) {
  if (token_.type_ == Token::NEWLINE && newline)
    Newline(NULL);

  const char kContinuation = makefile_flavor_ ? '\\' : '$';

  while (cur_ < end_) {
    if (*cur_ == ' ') {
      ++cur_;
    } else if (newline && *cur_ == '\n') {
      Newline(NULL);
    } else if (*cur_ == kContinuation && cur_ + 1 < end_ && cur_[1] == '\n') {
      ++cur_; ++cur_;
    } else if (*cur_ == '#' && cur_ == cur_line_) {
      while (cur_ < end_ && *cur_ != '\n')
        ++cur_;
      if (cur_ < end_ && *cur_ == '\n')
        ++cur_;
      cur_line_ = cur_;
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
    (c == '_') || (c == '$') ||
    (c == '(') || (c == ')') ||
    (c == '~') || (c == '\\');
}

bool Tokenizer::ExpectToken(Token::Type expected, string* err) {
  PeekToken();
  if (token_.type_ != expected)
    return ErrorExpected(Token(expected).AsString(), err);
  ConsumeToken();
  return true;
}

bool Tokenizer::ExpectIdent(const char* expected, string* err) {
  PeekToken();
  if (token_.type_ != Token::IDENT ||
      strncmp(token_.pos_, expected, token_.end_ - token_.pos_) != 0) {
    return ErrorExpected(string("'") + expected + "'", err);
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

// A note on backslashes in Makefiles, from reading the docs:
// Backslash-newline is the line continuation character.
// Backslash-# escapes a # (otherwise meaningful as a comment start).
// Backslash-% escapes a % (otherwise meaningful as a special).
// Finally, quoting the GNU manual, "Backslashes that are not in danger
// of quoting ‘%’ characters go unmolested."
// How do you end a line with a backslash?  The netbsd Make docs suggest
// reading the result of a shell command echoing a backslash!
//
// Rather than implement the above, we do the simpler thing here.
// If anyone actually has depfiles that rely on the more complicated
// behavior we can adjust this.
bool Tokenizer::ReadToNewline(string *text, string* err, size_t max_length) {
  // XXX token_.clear();
  const char kContinuation = makefile_flavor_ ? '\\' : '$';
  while (cur_ < end_ && *cur_ != '\n') {
    if (*cur_ == kContinuation) {
      // Might be a line continuation; peek ahead to check.
      if (cur_ + 1 >= end_)
        return Error("unexpected eof", err);
      if (*(cur_ + 1) == '\n') {
        // Let SkipWhitespace handle the continuation logic.
        SkipWhitespace();
        continue;
      }

      // Otherwise, just treat it like a normal character.
      text->push_back(*cur_);
      ++cur_;
    } else {
      text->push_back(*cur_);
      ++cur_;
    }
    if (text->size() >= max_length) {
      token_.pos_ = cur_;
      return false;
    }
  }
  return Newline(err);
}

// Hacky function to allow C:stuff, but still support 1 character filenames as
// left side of rule. See MakefileParser.OneLetterFilename and
// MakefileParser.ColonInFilename tests.
bool Tokenizer::LooksLikeFilenameDrive() const {
  return makefile_flavor_ &&
      cur_ - token_.pos_ == 1 &&
      *cur_ == ':' &&
      cur_ + 1 < end_ &&
      IsIdentChar(*(cur_ + 1));
}

Token::Type Tokenizer::PeekToken() {
  if (token_.type_ != Token::NONE)
    return token_.type_;

  token_.pos_ = cur_;
  if (!makefile_flavor_ && cur_indent_ == -1) {
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
    while (cur_ < end_ && (IsIdentChar(*cur_) || LooksLikeFilenameDrive())) {
      ++cur_;
    }
    token_.end_ = cur_;
    token_.type_ = Token::IDENT;
  } else if (*cur_ == ':') {
    token_.type_ = Token::COLON;
    ++cur_;
  } else if (*cur_ == '=') {
    token_.type_ = Token::EQUALS;
    ++cur_;
  } else if (*cur_ == '|') {
    if (cur_ + 1 < end_ && cur_[1] == '|') {
      token_.type_ = Token::PIPE2;
      cur_ += 2;
    } else {
      token_.type_ = Token::PIPE;
      ++cur_;
    }
  } else if (*cur_ == '\n') {
    token_.type_ = Token::NEWLINE;
    ++cur_;
    cur_line_ = cur_;
    cur_indent_ = -1;
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

MakefileParser::MakefileParser() {
  tokenizer_.SetMakefileFlavor();
}

bool MakefileParser::Parse(const string& input, string* err) {
  tokenizer_.Start(input.data(), input.data() + input.size());

  tokenizer_.SkipWhitespace(true);

  if (!tokenizer_.ReadIdent(&out_))
    return tokenizer_.ErrorExpected("output filename", err);
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
      case Token::IDENT: {
        const Token& token = tokenizer_.token();
        int len = token.end_ - token.pos_;
        if (len == 4 && memcmp(token.pos_, "rule", 4) == 0) {
          if (!ParseRule(err))
            return false;
        } else if (len == 5 && memcmp(token.pos_, "build", 5) == 0) {
          if (!ParseEdge(err))
            return false;
        } else if ((len == 7 && memcmp(token.pos_, "include", 7) == 0) ||
                   (len == 8 && memcmp(token.pos_, "subninja", 8) == 0)) {
          if (!ParseFileInclude(err))
            return false;
        } else {
          string name, value;
          if (!ParseLet(&name, &value, err))
            return false;
          env_->AddBinding(name, value);
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
  if (!tokenizer_.ExpectIdent("rule", err))
    return false;
  string name;
  if (!tokenizer_.ReadIdent(&name))
    return tokenizer_.ErrorExpected("rule name", err);
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
      const char* let_loc = tokenizer_.token_.pos_;

      string key;
      if (!ParseLetKey(&key, err))
        return false;

      EvalString* eval_target = NULL;
      if (key == "command") {
        eval_target = &rule->command_;
      } else if (key == "depfile") {
        eval_target = &rule->depfile_;
      } else if (key == "description") {
        eval_target = &rule->description_;
      } else {
        // Die on other keyvals for now; revisit if we want to add a
        // scope here.
        return tokenizer_.ErrorAt(let_loc, "unexpected variable '" + key + "'",
                                  err);
      }

      if (!ParseLetValue(eval_target, err))
        return false;
    }
    tokenizer_.ConsumeToken();
  }

  if (rule->command_.unparsed().empty())
    return tokenizer_.Error("expected 'command =' line", err);

  state_->AddRule(rule);
  return true;
}

bool ManifestParser::ParseLet(string* key, string* value, string* err) {
  if (!ParseLetKey(key, err))
    return false;

  EvalString eval;
  if (!ParseLetValue(&eval, err))
    return false;

  *value = eval.Evaluate(env_);

  return true;
}

bool ManifestParser::ParseLetKey(string* key, string* err) {
  if (!tokenizer_.ReadIdent(key))
    return tokenizer_.ErrorExpected("variable name", err);
  if (!tokenizer_.ExpectToken(Token::EQUALS, err))
    return false;
  return true;
}

bool ManifestParser::ParseLetValue(EvalString* eval, string* err) {
  // Backup the tokenizer state prior to consuming the line, for reporting
  // the source location in case of a parse error later.
  Tokenizer tokenizer_backup = tokenizer_;

  // XXX should we tokenize here?  it means we'll need to understand
  // command syntax, though...
  string value;
  if (!tokenizer_.ReadToNewline(&value, err))
    return false;

  string eval_err;
  size_t err_index;
  if (!eval->Parse(value, &eval_err, &err_index)) {
    value.clear();
    // Advance the saved tokenizer state up to the error index to report the
    // error at the correct source location.
    tokenizer_backup.ReadToNewline(&value, err, err_index);
    return tokenizer_backup.Error(eval_err, err);
  }

  return true;
}

bool ManifestParser::ParseEdge(string* err) {
  vector<string> ins, outs;

  if (!tokenizer_.ExpectIdent("build", err))
    return false;

  for (;;) {
    if (tokenizer_.PeekToken() == Token::COLON) {
      tokenizer_.ConsumeToken();
      break;
    }

    string out;
    if (!tokenizer_.ReadIdent(&out))
      return tokenizer_.ErrorExpected("output file list", err);
    outs.push_back(out);
  }
  // XXX check outs not empty

  string rule_name;
  if (!tokenizer_.ReadIdent(&rule_name))
    return tokenizer_.ErrorExpected("build command name", err);

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
  int implicit = 0;
  if (tokenizer_.PeekToken() == Token::PIPE) {
    tokenizer_.ConsumeToken();
    for (;;) {
      string in;
      if (!tokenizer_.ReadIdent(&in))
        break;
      ins.push_back(in);
      ++implicit;
    }
  }

  // Add all order-only deps, counting how many as we go.
  int order_only = 0;
  if (tokenizer_.PeekToken() == Token::PIPE2) {
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
      if (!ParseLet(&key, &val, err))
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
      string path = eval.Evaluate(env);
      if (!CanonicalizePath(&path, err))
        return false;
      *i = path;
    }
  }

  Edge* edge = state_->AddEdge(rule);
  edge->env_ = env;
  for (vector<string>::iterator i = ins.begin(); i != ins.end(); ++i)
    state_->AddIn(edge, *i);
  for (vector<string>::iterator i = outs.begin(); i != outs.end(); ++i)
    state_->AddOut(edge, *i);
  edge->implicit_deps_ = implicit;
  edge->order_only_deps_ = order_only;

  return true;
}

bool ManifestParser::ParseFileInclude(string* err) {
  string type;
  tokenizer_.ReadIdent(&type);

  string path;
  if (!tokenizer_.ReadIdent(&path))
    return tokenizer_.ErrorExpected("path to ninja file", err);

  string contents;
  string read_err;
  if (!file_reader_->ReadFile(path, &contents, &read_err))
    return tokenizer_.Error("loading " + path + ": " + read_err, err);

  ManifestParser subparser(state_, file_reader_);
  if (type == "subninja") {
    // subninja: Construct a new scope for the new parser.
    subparser.env_ = new BindingEnv;
    subparser.env_->parent_ = env_;
  } else {
    // include: Reuse the current scope.
    subparser.env_ = env_;
  }

  string sub_err;
  if (!subparser.Parse(contents, &sub_err))
    return tokenizer_.Error("in '" + path + "': " + sub_err, err);

  if (!tokenizer_.Newline(err))
    return false;

  return true;
}
