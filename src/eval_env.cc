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

#include "eval_env.h"
#include "util.h"
#include <stdio.h>

#define ENABLE_RESPONSE_FILE
//#define ENABLE_RESPONSE_FILE_TEST

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

vector<EvalString::TokenList> EvalString::splitChainedCommand() const
{
  TokenList cmd;
  vector<TokenList> cmds;

  // first check if we should start with a chell command
  for (TokenList::const_iterator i = parsed_.begin(); i != parsed_.end(); ++i) {
    if (i->second == SPLITTER) {
#ifdef _WIN32
      TokenList shell_call;
      shell_call.push_back(Token("cmd.exe /c ", RAW));
      cmds.push_back(shell_call);
#else
      // shell is always used
#endif
      break;
    }
  }

  for (TokenList::const_iterator i = parsed_.begin(); i != parsed_.end(); ++i) {
    if (i->second == SPLITTER) {
      cmds.push_back(cmd);
      TokenList splitter;
      splitter.push_back(*i);
      cmds.push_back(splitter);
      cmd.clear();
    } else {
      cmd.push_back(*i);
    }
  }
  cmds.push_back(cmd);
  return cmds;
}


string EvalString::PlainEvaluate(Env* env, const TokenList& tokens) const {
  string result;
  for (TokenList::const_iterator i = tokens.begin(); i != tokens.end(); ++i) {
    if (i->second == RAW || i->second == SPLITTER)
      result.append(i->first);
    else
      result.append(env->LookupVariable(i->first));
  }
  return result;
}

static size_t maximumCommandLineLength()
{
#ifdef ENABLE_RESPONSE_FILE_TEST
  return 200;
#endif
#ifdef _WIN32
  return 8100;
#else
  return 100000;
#endif
}

string EvalString::checkForArgumentFile(Env* env, const string& plaincmdstr, const TokenList& tokens) const
{
  if (plaincmdstr.size() < maximumCommandLineLength())
    return plaincmdstr;

  size_t rsp_pos = 0;
  for (TokenList::const_iterator i = tokens.begin(); i != tokens.end(); ++i) {
      if (i->first == "rsp")
        break;
      rsp_pos++;
  }

  size_t rsp_behind;
  if (rsp_pos == tokens.size()) {
    // no $rsp in build rule
    // place behind first token
    rsp_pos = 1;
    rsp_behind = 1;
  } else {
    rsp_behind = rsp_pos + 1;
  }

  // extract command until response file
  TokenList cmd;
  for (size_t i = 0; i < rsp_pos; i++) {
    cmd.push_back(tokens[i]);
  }

  // make the rule specific argument file loading option
  string rspopt = (env->rspopt_.empty() ? " @" : env->rspopt_);
  cmd.push_back(Token(rspopt, RAW));
  cmd.push_back(Token("rsp", SPECIAL));

  // remaining arguments
  TokenList arg;
  for (size_t i = rsp_behind; i < tokens.size(); i++) {
    arg.push_back(tokens[i]);
  }

  string err;
  const FileInfo tmp = TempFilename(&err);
  env->current_rsp_file_ = tmp.path;
  string cmdstr = PlainEvaluate(env, cmd);
  string argstr = PlainEvaluate(env, arg);
  WriteFile(tmp, argstr, &err);
  env->rsp_files_.push_back(env->current_rsp_file_); // TODO not all files are removed
  env->current_rsp_file_ = "";

#ifdef ENABLE_RESPONSE_FILE_TEST
  if (argstr.find("ar ") != string::npos)
    return plaincmdstr;
  printf("\nwith response file: %s \n", cmdstr.c_str());
  //printf("\n     :'%s\n'", argstr.c_str());
#endif

  return cmdstr;
}


string EvalString::Evaluate(Env* env) const {
#if defined(_WIN32) || defined(ENABLE_RESPONSE_FILE)
  string result;
  vector<TokenList> cmds = splitChainedCommand();
  for (size_t i = 0; i < cmds.size(); i++) {
      string cmdstr = PlainEvaluate(env, cmds[i]);
      cmdstr = checkForArgumentFile(env, cmdstr, cmds[i]);
      result.append(cmdstr);
  }
  return result;
#else
  return PlainEvaluate(env, parsed_);
#endif
}

void EvalString::AddText(StringPiece text) {
  // Add it to the end of an existing RAW token if possible.
  if (!parsed_.empty() && parsed_.back().second == RAW) {
    parsed_.back().first.append(text.str_, text.len_);
  } else {
    parsed_.push_back(make_pair(text.AsString(), RAW));
  }
}
void EvalString::AddSpecial(StringPiece text) {
  parsed_.push_back(make_pair(text.AsString(), SPECIAL));
}
void EvalString::AddSplitter(StringPiece text) {
  parsed_.push_back(make_pair(text.AsString(), SPLITTER));
}

string EvalString::Serialize() const {
  string result;
  for (TokenList::const_iterator i = parsed_.begin();
       i != parsed_.end(); ++i) {
    result.append("[");
    if (i->second == SPECIAL)
      result.append("$");
    result.append(i->first);
    result.append("]");
  }
  return result;
}
