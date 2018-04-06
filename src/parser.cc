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

bool Parser::Load(const std::string& filename, std::string* err, Lexer* parent) {
  METRIC_RECORD(".ninja parse");
  std::string contents;
  std::string read_err;
  if (file_reader_->ReadFile(filename, &contents, &read_err) !=
      FileReader::Okay) {
    *err = "loading '" + filename + "': " + read_err;
    if (parent)
      parent->Error(std::string(*err), err);
    return false;
  }

  return Parse(filename, contents, err);
}

bool Parser::ExpectToken(Lexer::Token expected, std::string* err) {
  Lexer::Token token = lexer_.ReadToken();
  if (token != expected) {
    std::string message = std::string("expected ") + Lexer::TokenName(expected);
    message += std::string(", got ") + Lexer::TokenName(token);
    message += Lexer::TokenErrorHint(expected);
    return lexer_.Error(message, err);
  }
  return true;
}
