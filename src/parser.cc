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

using namespace std;

bool Parser::Load(const string& filename, string* err, Lexer* parent) {
  // If |parent| is not NULL, metrics collection has been started by a parent
  // Parser::Load() in our call stack. Do not start a new one here to avoid
  // over-counting parsing times.
  METRIC_RECORD_IF(".ninja parse", parent == NULL);
  string contents;
  string read_err;
  if (file_reader_->ReadFile(filename, &contents, &read_err) !=
      FileReader::Okay) {
    *err = "loading '" + filename + "': " + read_err;
    if (parent)
      parent->Error(string(*err), err);
    return false;
  }

  return Parse(filename, contents, err);
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
