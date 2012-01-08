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

#include "deplist.h"

#include <errno.h>
#include <stdio.h>

#include "depfile_parser.h"
#include "showincludes_parser.h"
#include "util.h"

#ifdef WIN32
#include "getopt.h"
#else
#include <getopt.h>
#endif

namespace {

void Usage() {
  printf(
"ninja-deplist-helper: convert dependency output into ninja deplist format.\n"
"\n"
"usage: ninja-deplist-helper [options] [infile]\n"
"options:\n"
"  -f FORMAT  specify input format; formats are\n"
"               gcc  gcc Makefile-like output\n"
"               cl   MSVC cl.exe /showIncludes output\n"
"  -o FILE  write output to FILE (default: stdout)\n"
         );
}

enum InputFormat {
  INPUT_DEPFILE,
  INPUT_SHOW_INCLUDES
};

}  // anonymous namespace

int main(int argc, char** argv) {
  const char* output_filename = NULL;
  InputFormat input_format = INPUT_DEPFILE;

  const option kLongOptions[] = {
    { "help", no_argument, NULL, 'h' },
    { NULL, 0, NULL, 0 }
  };
  int opt;
  while ((opt = getopt_long(argc, argv, "f:o:h", kLongOptions, NULL)) != -1) {
    switch (opt) {
      case 'f': {
        string format = optarg;
        if (format == "gcc")
          input_format = INPUT_DEPFILE;
        else if (format == "cl")
          input_format = INPUT_SHOW_INCLUDES;
        else
          Fatal("unknown input format '%s'", format.c_str());
        break;
      }
      case 'o':
        output_filename = optarg;
        break;
      case 'h':
      default:
        Usage();
        return 0;
    }
  }
  argv += optind;
  argc -= optind;

  if (argc < 1) {
    Usage();
    return 1;
  }

  const char* input_filename = argv[0];

  // Read and parse input file.
  string content;
  string err;
  if (ReadFile(input_filename, &content, &err) < 0)
    Fatal("loading %s: %s", input_filename, err.c_str());

  StringPiece target;
  vector<StringPiece> inputs;
  switch (input_format) {
  case INPUT_DEPFILE:
    if (!DepfileParser::Parse(&content, &target, &inputs, &err))
      Fatal("parsing %s: %s", input_filename, err.c_str());
    break;
  case INPUT_SHOW_INCLUDES:
    string text = ShowIncludes::Filter(content, &inputs);
    printf("%s", text.c_str());
    break;
  }

  // Open/write/close output file.
  FILE* output = stdout;
  if (output_filename) {
    output = fopen(output_filename, "wb");
    if (!output)
      Fatal("opening %s: %s", output_filename, strerror(errno));
  }
  if (!Deplist::Write(output, inputs))
    Fatal("error writing %s");
  if (output_filename) {
    if (fclose(output) < 0)
      Fatal("fclose(%s): %s", output_filename, strerror(errno));
  }

  return 0;
}
