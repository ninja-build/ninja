// Copyright 2018 Google Inc. All Rights Reserved.
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

#include "parser.h"

#include "disk_interface.h"
#include "metrics.h"
#include "util.h"

#include <string.h>
#include "thirdparty/inja/inja.hpp"

using namespace std;

void registerCallbacks(inja::Environment& env) {
  env.add_void_callback("warning", 1, [](inja::Arguments& args) {
    Warning(args.at(0)->get<string>().c_str());
  });
  return;
}

bool Parser::Load(const string& filename, const std::string& param_filename, string* err, Lexer* parent) {
  METRIC_RECORD(".ninja parse");
  string contents;
  string read_err;
  if (file_reader_->ReadFile(filename, &contents, &read_err) !=
      FileReader::Okay) {
    *err = "loading '" + filename + "': " + read_err;
    if (parent)
      parent->Error(string(*err), err);
    return false;
  }

  // The lexer needs a nul byte at the end of its input, to know when it's done.
  // It takes a StringPiece, and StringPiece's string constructor uses
  // string::data().  data()'s return value isn't guaranteed to be
  // null-terminated (although in practice - libc++, libstdc++, msvc's stl --
  // it is, and C++11 demands that too), so add an explicit nul byte.
  contents.resize(contents.size() + 1);

  // render template if specified
  if (param_filename.compare("") != 0) {
    string json_contents;
    if (file_reader_->ReadFile(param_filename, &json_contents, &read_err) !=
        FileReader::Okay) {
      *err = "loading '" + param_filename + "': " + read_err;
      if (parent)
        parent->Error(string(*err), err);
      return false;
    }

    inja::Environment env;
    registerCallbacks(env);

    inja::json data = inja::json::parse(json_contents);
    contents = env.render(contents, data);
  }

  return Parse(filename, param_filename, contents, err);
}

bool Parser::ExpectToken(Lexer::Token expected, string* err) {
  Lexer::Token token = lexer_.ReadToken();
  if (token != expected) {
    string message = string("expected ") + Lexer::TokenName(expected);
    message += string(", got ") + Lexer::TokenName(token);
    message += Lexer::TokenErrorHint(expected);
    return lexer_.Error(message, err);
  }
  return true;
}
