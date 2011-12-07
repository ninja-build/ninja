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

#include "depfile_parser.h"

bool DepfileParser::Parse(const string& content, string* err) {
  const char* p = content.data();
  const char* end = content.data() + content.size();
  for (;;) {
    const char* start = p;
    char yych;
    /*!re2c
    re2c:define:YYCTYPE = "const char";
    re2c:define:YYCURSOR = p;
    re2c:define:YYMARKER = q;
    re2c:define:YYLIMIT = end;

    re2c:yyfill:parameter = 0;
    re2c:define:YYFILL = break;

    re2c:indent:top = 2;
    re2c:indent:string = "  ";

    re2c:yych:emit = 0;

    '\\\n' { continue; }
    [ \n]* { continue; }
    [a-zA-Z0-9+,/_:.-]+ {
      // Got a filename.
      if (p[-1] == ':') {
        out_ = StringPiece(start, p - start - 1);
      } else {
        ins_.push_back(StringPiece(start, p - start));
      }
      continue;
    }
    [^] {
      *err = "BUG: depfile lexer encountered unknown state";
      return false;
    }
    */
  }
  return true;
}
