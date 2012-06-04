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
#include "metrics.h"
#include "state.h"
#include "util.h"

ManifestParser::ManifestParser(State* state, FileReader* file_reader)
  : state_(state), file_reader_(file_reader) {
  env_ = &state->bindings_;
}
bool ManifestParser::Load(const string& filename, string* err) {
  string contents;
  string read_err;
  if (!file_reader_->ReadFile(filename, &contents, &read_err)) {
    *err = "loading '" + filename + "': " + read_err;
    return false;
  }
  contents.resize(contents.size() + 10);
  return Parse(filename, contents, err);
}

bool ManifestParser::Parse(const string& filename, const string& input,
                           string* err) {
  METRIC_RECORD(".ninja parse");
  lexer_.Start(filename, input);

  for (;;) {
    Lexer::Token token = lexer_.ReadToken();
    switch (token) {
    case Lexer::BUILD:
      if (!ParseEdge(err))
        return false;
      break;
    case Lexer::RULE:
      if (!ParseRule(err))
        return false;
      break;
    case Lexer::DEFAULT:
      if (!ParseDefault(err))
        return false;
      break;
    case Lexer::IDENT: {
      lexer_.UnreadToken();
      string name;
      EvalString value;
      if (!ParseLet(&name, &value, err))
        return false;
      env_->AddBinding(name, value.Evaluate(env_));
      break;
    }
    case Lexer::INCLUDE:
      if (!ParseFileInclude(false, err))
        return false;
      break;
    case Lexer::SUBNINJA:
      if (!ParseFileInclude(true, err))
        return false;
      break;
    case Lexer::ERROR:
      return lexer_.Error("lexing error", err);
    case Lexer::TEOF:
      return true;
    case Lexer::NEWLINE:
      break;
    default:
      return lexer_.Error(string("unexpected ") + Lexer::TokenName(token),
                          err);
    }
  }
  return false;  // not reached
}

bool ManifestParser::ParseRule(string* err) {
  string name;
  if (!lexer_.ReadIdent(&name))
    return lexer_.Error("expected rule name", err);

  if (!ExpectToken(Lexer::NEWLINE, err))
    return false;

  if (state_->LookupRule(name) != NULL) {
    *err = "duplicate rule '" + name + "'";
    return false;
  }

  Rule* rule = new Rule(name);  // XXX scoped_ptr

  while (lexer_.PeekToken(Lexer::INDENT)) {
    string key;
    EvalString value;
    if (!ParseLet(&key, &value, err))
      return false;

    if (key == "command") {
      rule->command_ = value;
    } else if (key == "depfile") {
      rule->depfile_ = value;
    } else if (key == "description") {
      rule->description_ = value;
    } else if (key == "generator") {
      rule->generator_ = true;
    } else if (key == "restat") {
      rule->restat_ = true;
    } else if (key == "rspfile") {
      rule->rspfile_ = value;
    } else if (key == "rspfile_content") {
      rule->rspfile_content_ = value;
    } else {
      // Die on other keyvals for now; revisit if we want to add a
      // scope here.
      return lexer_.Error("unexpected variable '" + key + "'", err);
    }
  }

  if (rule->rspfile_.empty() != rule->rspfile_content_.empty())
    return lexer_.Error("rspfile and rspfile_content need to be both specified", err);

  if (rule->command_.empty())
    return lexer_.Error("expected 'command =' line", err);

  state_->AddRule(rule);
  return true;
}

bool ManifestParser::ParseLet(string* key, EvalString* value, string* err) {
  if (!lexer_.ReadIdent(key))
    return false;
  if (!ExpectToken(Lexer::EQUALS, err))
    return false;
  if (!lexer_.ReadVarValue(value, err))
    return false;
  return true;
}

bool ManifestParser::ParseDefault(string* err) {
  EvalString eval;
  if (!lexer_.ReadPath(&eval, err))
    return false;
  if (eval.empty())
    return lexer_.Error("expected target name", err);

  do {
    string path = eval.Evaluate(env_);
    string path_err;
    if (!CanonicalizePath(&path, &path_err))
      return lexer_.Error(path_err, err);
    if (!state_->AddDefault(path, &path_err))
      return lexer_.Error(path_err, err);

    eval.Clear();
    if (!lexer_.ReadPath(&eval, err))
      return false;
  } while (!eval.empty());

  if (!ExpectToken(Lexer::NEWLINE, err))
    return false;

  return true;
}

bool ManifestParser::ParseEdge(string* err) {
  vector<EvalString> ins, outs;

  {
    EvalString out;
    if (!lexer_.ReadPath(&out, err))
      return false;
    if (out.empty())
      return lexer_.Error("expected path", err);

    do {
      outs.push_back(out);

      out.Clear();
      if (!lexer_.ReadPath(&out, err))
        return false;
    } while (!out.empty());
  }

  if (!ExpectToken(Lexer::COLON, err))
    return false;

  string rule_name;
  if (!lexer_.ReadIdent(&rule_name))
    return lexer_.Error("expected build command name", err);

  const Rule* rule = state_->LookupRule(rule_name);
  if (!rule)
    return lexer_.Error("unknown build rule '" + rule_name + "'", err);

  for (;;) {
    // XXX should we require one path here?
    EvalString in;
    if (!lexer_.ReadPath(&in, err))
      return false;
    if (in.empty())
      break;
    ins.push_back(in);
  }

  // Add all implicit deps, counting how many as we go.
  int implicit = 0;
  if (lexer_.PeekToken(Lexer::PIPE)) {
    for (;;) {
      EvalString in;
      if (!lexer_.ReadPath(&in, err))
        return err;
      if (in.empty())
        break;
      ins.push_back(in);
      ++implicit;
    }
  }

  // Add all order-only deps, counting how many as we go.
  int order_only = 0;
  if (lexer_.PeekToken(Lexer::PIPE2)) {
    for (;;) {
      EvalString in;
      if (!lexer_.ReadPath(&in, err))
        return false;
      if (in.empty())
        break;
      ins.push_back(in);
      ++order_only;
    }
  }

  if (!ExpectToken(Lexer::NEWLINE, err))
    return false;

  // Default to using outer env.
  BindingEnv* env = env_;

  // But create and fill a nested env if there are variables in scope.
  if (lexer_.PeekToken(Lexer::INDENT)) {
    // XXX scoped_ptr to handle error case.
    env = new BindingEnv(env_);
    do {
      string key;
      EvalString val;
      if (!ParseLet(&key, &val, err))
        return false;
      env->AddBinding(key, val.Evaluate(env_));
    } while (lexer_.PeekToken(Lexer::INDENT));
  }

  Edge* edge = state_->AddEdge(rule);
  edge->env_ = env;
  for (vector<EvalString>::iterator i = ins.begin(); i != ins.end(); ++i) {
    string path = i->Evaluate(env);
    string path_err;
    if (!CanonicalizePath(&path, &path_err))
      return lexer_.Error(path_err, err);
    state_->AddIn(edge, path);
  }
  for (vector<EvalString>::iterator i = outs.begin(); i != outs.end(); ++i) {
    string path = i->Evaluate(env);
    string path_err;
    if (!CanonicalizePath(&path, &path_err))
      return lexer_.Error(path_err, err);
    state_->AddOut(edge, path);
  }
  edge->implicit_deps_ = implicit;
  edge->order_only_deps_ = order_only;

  return true;
}

bool ManifestParser::ParseFileInclude(bool new_scope, string* err) {
  // XXX this should use ReadPath!
  EvalString eval;
  if (!lexer_.ReadPath(&eval, err))
    return false;
  string path = eval.Evaluate(env_);

  string contents;
  string read_err;
  if (!file_reader_->ReadFile(path, &contents, &read_err))
    return lexer_.Error("loading '" + path + "': " + read_err, err);

  ManifestParser subparser(state_, file_reader_);
  if (new_scope) {
    subparser.env_ = new BindingEnv(env_);
  } else {
    subparser.env_ = env_;
  }

  if (!subparser.Parse(path, contents, err))
    return false;

  if (!ExpectToken(Lexer::NEWLINE, err))
    return false;

  return true;
}

bool ManifestParser::ExpectToken(Lexer::Token expected, string* err) {
  Lexer::Token token = lexer_.ReadToken();
  if (token != expected) {
    string message = string("expected ") + Lexer::TokenName(expected);
    message += string(", got ") + Lexer::TokenName(token);
    message += Lexer::TokenErrorHint(expected);
    return lexer_.Error(message, err);
  }
  return true;
}
