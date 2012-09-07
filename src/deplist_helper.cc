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

#ifndef _WIN32
#error deplist_helper is only for Win32.
#endif

#include "deplist.h"

#include <algorithm>
#include <errno.h>
#include <stdio.h>

#include "dep_database.h"
#include "includes_normalize.h"
#include "showincludes_parser.h"
#include "subprocess.h"
#include "string_piece.h"
#include "util.h"

#include "getopt.h"

namespace {

void Usage() {
  printf(
"ninja-deplist-helper: convert dependency output into ninja deplist format.\n"
"\n"
"usage: ninja-deplist-helper [options] [infile|command]\n"
"options:\n"
"  -q         suppress first line of output in cl mode. this will be the file\n"
"             being compiled when /nologo is used.\n"
"  -r BASE    normalize paths and make relative to BASE before outputting\n"
"  -o FILE    write output to FILE (default: stdout)\n"
"  -e ENVFILE replace KEY=value lines in ENVFILE to use as environment.\n"
"             only applicable when -c is used\n"
"  --command  run command via CreateProcess to get output rather than an infile\n"
"             must be the last argument\n"
         );
}

}  // anonymous namespace

void PushPathIntoEnvironment(void* env_block) {
  const char* as_str = reinterpret_cast<const char*>(env_block);
  while (as_str[0]) {
    if (_strnicmp(as_str, "path=", 5) == 0) {
      _putenv(as_str);
      return;
    } else {
      as_str = &as_str[strlen(as_str) + 1];
    }
  }
}

int main(int argc, char** argv) {
  const char* output_filename = NULL;
  const char* relative_to = NULL;
  const char* envfile = NULL;
  bool quiet = false;
  bool run_command = false;

  const option kLongOptions[] = {
    { "help", no_argument, NULL, 'h' },
    { "command", no_argument, NULL, 'C' },
    { NULL, 0, NULL, 0 }
  };
  int opt;
  while ((opt = getopt_long(argc, argv, "f:o:hqd:r:e:", kLongOptions, NULL)) != -1) {
    switch (opt) {
      case 'f': {
        // Ignored temporarily for backwards compatibility.
        break;
      }
      case 'o':
        output_filename = optarg;
        break;
      case 'e':
        envfile = optarg;
        break;
      case 'C':
        run_command = true;
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

  string content;
  string err;
  int returncode = 0;
  if (run_command) {
    string env;
    void* env_block = NULL;
    if (envfile) {
      if (ReadFile(envfile, &env, &err, true) != 0)
        Fatal("couldn't open %s: %s", envfile, err.c_str());
      env_block = const_cast<void*>(static_cast<const void*>(env.data()));
      PushPathIntoEnvironment(env_block);
    }
    SubprocessSet subprocs;
    char* command = GetCommandLine();
    // TODO(scottmg): hack!
    command = strstr(command, " --command ");
    if (command)
      command += 11;
    Subprocess* subproc = subprocs.Add(command, env_block);
    if (!subproc)
      Fatal("couldn't start: %s", command);
    while (!subproc->Done()) {
      subprocs.DoWork();
    }
    returncode = subproc->Finish();
    content = subproc->GetOutput();
  } else {
    FILE* input = stdin;
    const char* input_filename = argc > 0 ? argv[0] : NULL;
    if (input_filename) {
      input = fopen(input_filename, "rb");
      if (!input)
        Fatal("opening %s: %s", input_filename, strerror(errno));
    }

    // Read and parse input file.
    if (ReadFile(input, &content, &err) != 0)
      Fatal("loading %s: %s", input_filename, err.c_str());

    if (input_filename) {
      if (fclose(input) < 0)
        Fatal("fclose(%s): %s", input_filename, strerror(errno));
    }
  }

  vector<StringPiece> includes;
  vector<StringPiece> ins;
  vector<string> normalized;
  string depfile_err;
  if (quiet) {
    size_t at;
    if (
        (at = content.find(".c\r\n")) != string::npos ||
        (at = content.find(".cc\r\n")) != string::npos ||
        (at = content.find(".cxx\r\n")) != string::npos ||
        (at = content.find(".cpp\r\n")) != string::npos ||
        (at = content.find(".c\n")) != string::npos ||
        (at = content.find(".cc\n")) != string::npos ||
        (at = content.find(".cxx\n")) != string::npos ||
        (at = content.find(".cpp\n")) != string::npos
        ) {
      content = content.substr(content.find("\n", at) + 1);
    }
  }
  string text = ShowIncludes::Filter(content, &includes);
  for (vector<StringPiece>::iterator i(includes.begin()); i != includes.end(); ++i) {
    // TODO: AsString temp (maybe this is dead code anyway?)
    normalized.push_back(IncludesNormalize::Normalize(i->AsString(), relative_to));
  }
  for (size_t i = 0; i < normalized.size(); ++i)
    ins.push_back(normalized[i]);
  printf("%s", text.c_str());

  const char* db_filename = ".ninja_depdb";
  if (!output_filename)
      Fatal("-o required");
  DepDatabase depdb(db_filename, false);
  Deplist::WriteDatabase(depdb, output_filename, ins);

  return returncode;
}
