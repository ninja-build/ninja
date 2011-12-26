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
bool DepfileParser::Parse(const string& content, string* err) {
  const char* p = content.data();
  const char* q = p;
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
    [ \n]+ { continue; }
    [a-zA-Z0-9+,/\\_:.-]+ {
      // Got a filename.
      int len = p - start;;
      if (start[len - 1] == ':')
        len--;  // Strip off trailing colon, if any.

      if (len == 0)
        continue;  // Drop isolated colons.

      if (out_.empty()) {
        out_ = string(start, len);
      } else {
        ins_.push_back(string(start, len));
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
