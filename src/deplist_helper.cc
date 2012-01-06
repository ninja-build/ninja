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

#include <stdio.h>

#include "deplist.h"
#include "util.h"

namespace {

void Usage() {
  printf(
"ninja-deplist-helper: convert dependency output into ninja deplist format.\n"
"\n"
"usage: ninja-deplist-helper [options] infile\n"
"options:\n"
//"  -f FORMAT  specify input format
"  -o FILE  write output to FILE (default: stdout)\n"
         );
}

}  // anonymous namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    Usage();
    return 1;
  }

  const char* infile = argv[1];

  DepfileParser parser;
  string content;
  string err;
  if (ReadFile(infile, &content, &err) < 0)
    Fatal("loading %s: %s", infile, err.c_str());
  if (!parser.Parse(&content, &err))
    Fatal("parsing %s: %s", infile, err.c_str());

  FILE* output = stdout;
  /*
  FILE* f = fopen(filename.c_str(), "wb");
  if (!f) {
    *err = "opening " + filename + ": " + strerror(errno);
    return false;
  }
  */

  if (!Deplist::Write(output, parser.ins_))
    Fatal("error writing %s");

  return 0;
}
