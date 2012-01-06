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

#include <errno.h>
#include <stdio.h>

#include "deplist.h"
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
"usage: ninja-deplist-helper [options] infile\n"
"options:\n"
//"  -f FORMAT  specify input format
"  -o FILE  write output to FILE (default: stdout)\n"
         );
}

}  // anonymous namespace

int main(int argc, char** argv) {
  const char* output_filename = NULL;

  const option kLongOptions[] = {
    { "help", no_argument, NULL, 'h' },
    { NULL, 0, NULL, 0 }
  };
  int opt;
  while ((opt = getopt_long(argc, argv, "o:h", kLongOptions, NULL)) != -1) {
    switch (opt) {
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
  DepfileParser parser;
  string content;
  string err;
  if (ReadFile(input_filename, &content, &err) < 0)
    Fatal("loading %s: %s", input_filename, err.c_str());
  if (!parser.Parse(&content, &err))
    Fatal("parsing %s: %s", input_filename, err.c_str());

  // Open/write/close output file.
  FILE* output = stdout;
  if (output_filename) {
    output = fopen(output_filename, "wb");
    if (!output)
      Fatal("opening %s: %s", output_filename, strerror(errno));
  }
  if (!Deplist::Write(output, parser.ins_))
    Fatal("error writing %s");
  if (output_filename) {
    if (fclose(output) < 0)
      Fatal("fclose(%s): %s", output_filename, strerror(errno));
  }

  return 0;
}
