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

#include "msvc_helper.h"

#include <stdio.h>
#include <windows.h>

#include "util.h"

#include "getopt.h"

namespace {

void Usage() {
  printf(
"usage: ninja -t msvc [options] -- cl.exe /showIncludes /otherArgs\n"
"options:\n"
"  -e ENVFILE load environment block from ENVFILE as environment\n"
"  -o FILE    write output dependency information to FILE.d\n"
         );
}

void PushPathIntoEnvironment(const string& env_block) {
  const char* as_str = env_block.c_str();
  while (as_str[0]) {
    if (_strnicmp(as_str, "path=", 5) == 0) {
      _putenv(as_str);
      return;
    } else {
      as_str = &as_str[strlen(as_str) + 1];
    }
  }
}

}  // anonymous namespace

int MSVCHelperMain(int argc, char** argv) {
  const char* output_filename = NULL;
  const char* envfile = NULL;

  const option kLongOptions[] = {
    { "help", no_argument, NULL, 'h' },
    { NULL, 0, NULL, 0 }
  };
  int opt;
  while ((opt = getopt_long(argc, argv, "e:o:h", kLongOptions, NULL)) != -1) {
    switch (opt) {
      case 'e':
        envfile = optarg;
        break;
      case 'o':
        output_filename = optarg;
        break;
      case 'h':
      default:
        Usage();
        return 0;
    }
  }

  if (!output_filename) {
    Usage();
    Fatal("-o required");
  }

  string env;
  if (envfile) {
    string err;
    if (ReadFile(envfile, &env, &err) != 0)
      Fatal("couldn't open %s: %s", envfile, err.c_str());
    PushPathIntoEnvironment(env);
  }

  char* command = GetCommandLine();
  command = strstr(command, " -- ");
  if (!command) {
    Fatal("expected command line to end with \" -- command args\"");
  }
  command += 4;

  CLWrapper cl;
  if (!env.empty())
    cl.SetEnvBlock((void*)env.data());
  int exit_code = cl.Run(command);

  string depfile = string(output_filename) + ".d";
  FILE* output = fopen(depfile.c_str(), "w");
  if (!output) {
    Fatal("opening %s: %s", depfile.c_str(), GetLastErrorString().c_str());
  }
  fprintf(output, "%s: ", output_filename);
  vector<string> headers = cl.GetEscapedResult();
  for (vector<string>::iterator i = headers.begin(); i != headers.end(); ++i) {
    fprintf(output, "%s\n", i->c_str());
  }
  fclose(output);

  return exit_code;
}
