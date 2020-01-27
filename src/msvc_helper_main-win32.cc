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

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <windows.h>

#include "clparser.h"
#include "util.h"

#include "getopt.h"

namespace ninja {

void PushPathIntoEnvironment(const std::string& env_block) {
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

void WriteDepFileOrDie(const char* object_path, const CLParser& parse) {
  std::string depfile_path = std::string(object_path) + ".d";
  FILE* depfile = fopen(depfile_path.c_str(), "w");
  if (!depfile) {
    unlink(object_path);
    Fatal("opening %s: %s", depfile_path.c_str(),
          GetLastErrorString().c_str());
  }
  if (fprintf(depfile, "%s: ", object_path) < 0) {
    unlink(object_path);
    fclose(depfile);
    unlink(depfile_path.c_str());
    Fatal("writing %s", depfile_path.c_str());
  }
  const std::set<std::string>& headers = parse.includes_;
  for (std::set<std::string>::const_iterator i = headers.begin();
       i != headers.end(); ++i) {
    if (fprintf(depfile, "%s\n", EscapeForDepfile(*i).c_str()) < 0) {
      unlink(object_path);
      fclose(depfile);
      unlink(depfile_path.c_str());
      Fatal("writing %s", depfile_path.c_str());
    }
  }
  fclose(depfile);
}

}  // anonymous namespace

int MSVCHelperMain(const std::string& deps_prefix, const std::string& envfile, const std::string& output_filename) {
  std::string env;
  if (envfile) {
    std::string err;
    if (ReadFile(envfile, &env, &err) != 0)
      Fatal("couldn't open %s: %s", envfile, err.c_str());
    PushPathIntoEnvironment(env);
  }

  char* command = GetCommandLineA();
  command = strstr(command, " -- ");
  if (!command) {
    Fatal("expected command line to end with \" -- command args\"");
  }
  command += 4;

  CLWrapper cl;
  if (!env.empty())
    cl.SetEnvBlock((void*)env.data());
  std::string output;
  int exit_code = cl.Run(command, &output);

  if (output_filename) {
    CLParser parser;
    std::string err;
    if (!parser.Parse(output, deps_prefix, &output, &err))
      Fatal("%s\n", err.c_str());
    WriteDepFileOrDie(output_filename, parser);
  }

  if (output.empty())
    return exit_code;

  // CLWrapper's output already as \r\n line endings, make sure the C runtime
  // doesn't expand this to \r\r\n.
  _setmode(_fileno(stdout), _O_BINARY);
  // Avoid printf and C strings, since the actual output might contain null
  // bytes like UTF-16 does (yuck).
  fwrite(&output[0], 1, output.size(), stdout);

  return exit_code;
}
}  // namespace ninja
