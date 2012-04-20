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

#include <algorithm>
#include <errno.h>
#include <stdio.h>

#include "dep_database.h"
#include "depfile_parser.h"
#include "includes_normalize.h"
#include "showincludes_parser.h"
#include "util.h"

#ifdef _WIN32
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
"  -q         suppress first line of output in cl mode. this will be the file\n"
"             being compiled when /nologo is used.\n"
"  -d FILE    write to database FILE instead of individual file\n"
"             requires -o to specify target index name\n"
"  -r BASE    normalize paths and make relative to BASE before outputting\n"
"  -o FILE    write output to FILE (default: stdout)\n"
         );
}

enum InputFormat {
  INPUT_DEPFILE,
  INPUT_SHOW_INCLUDES
};

}  // anonymous namespace

int main(int argc, char** argv) {
  const char* output_filename = NULL;
  const char* relative_to = NULL;
  InputFormat input_format = INPUT_DEPFILE;
  bool quiet = false;

  const option kLongOptions[] = {
    { "help", no_argument, NULL, 'h' },
    { NULL, 0, NULL, 0 }
  };
  int opt;
  while ((opt = getopt_long(argc, argv, "f:o:hqd:r:", kLongOptions, NULL)) != -1) {
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
      case 'q':
        quiet = true;
        break;
      case 'r':
        relative_to = optarg;
        break;
      case 'h':
      default:
        Usage();
        return 0;
    }
  }
  argv += optind;
  argc -= optind;

  FILE* input = stdin;
  const char* input_filename = argc > 0 ? argv[0] : NULL;
  if (input_filename) {
    input = fopen(input_filename, "rb");
    if (!input)
      Fatal("opening %s: %s", input_filename, strerror(errno));
  }

  // Read and parse input file.
  string content;
  string err;
  if (!ReadFile(input, &content, &err))
    Fatal("loading %s: %s", input_filename, err.c_str());

  if (input_filename) {
    if (fclose(input) < 0)
      Fatal("fclose(%s): %s", input_filename, strerror(errno));
  }

  DepfileParser depfile;
  vector<StringPiece> includes;
  vector<string> normalized;
  string depfile_err;
  switch (input_format) {
  case INPUT_DEPFILE:
    if (!depfile.Parse(&content, &depfile_err))
      Fatal("parsing %s: %s", input_filename, err.c_str());
    break;
  case INPUT_SHOW_INCLUDES:
    if (quiet) {
      size_t at;
      if ((at = content.find(".c\n")) != string::npos ||
          (at = content.find(".cc\n")) != string::npos ||
          (at = content.find(".cxx\n")) != string::npos ||
          (at = content.find(".cpp\n")) != string::npos) {
        content = content.substr(content.find("\n", at) + 1);
      }
    }
    string text = ShowIncludes::Filter(content, &includes);
    for (vector<StringPiece>::iterator i(includes.begin()); i != includes.end(); ++i)
      normalized.push_back(IncludesNormalize::Normalize(*i, relative_to));
    for (size_t i = 0; i < normalized.size(); ++i)
      depfile.ins_.push_back(normalized[i]);
    printf("%s", text.c_str());
    break;
  }

  const char* db_filename = ".ninja_depdb";
  if (db_filename) {
    if (!output_filename)
      Fatal("-d requires -o");
    DepDatabase depdb(db_filename, false);
    Deplist::WriteDatabase(depdb, output_filename, depfile.ins_);
  } else {
    // Open/write/close output file.
    FILE* output = stdout;
    if (output_filename) {
      output = fopen(output_filename, "wb");
      if (!output)
        Fatal("opening %s: %s", output_filename, strerror(errno));
    }
    if (!Deplist::Write(output, depfile.ins_))
      Fatal("error writing %s");
    if (output_filename) {
      if (fclose(output) < 0)
        Fatal("fclose(%s): %s", output_filename, strerror(errno));
    }
  }

  return 0;
}
