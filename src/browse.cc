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

#include "browse.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>

#include "build/browse_py.h"

void RunBrowsePython(State* state, const char* ninja_command,
                     const char* input_file, int argc, char* argv[]) {
  // Fork off a Python process and have it run our code via its stdin.
  // (Actually the Python process becomes the parent.)
  int pipefd[2];
  if (pipe(pipefd) < 0) {
    perror("ninja: pipe");
    return;
  }

  pid_t pid = fork();
  if (pid < 0) {
    perror("ninja: fork");
    return;
  }

  if (pid > 0) {  // Parent.
    close(pipefd[1]);
    do {
      if (dup2(pipefd[0], 0) < 0) {
        perror("ninja: dup2");
        break;
      }

      std::vector<const char *> command;
      command.push_back(NINJA_PYTHON);
      command.push_back("-");
      command.push_back("--ninja-command");
      command.push_back(ninja_command);
      command.push_back("-f");
      command.push_back(input_file);
      for (int i = 0; i < argc; i++) {
          command.push_back(argv[i]);
      }
      command.push_back(NULL);
      execvp(command[0], (char**)&command[0]);
      if (errno == ENOENT) {
        printf("ninja: %s is required for the browse tool\n", NINJA_PYTHON);
      } else {
        perror("ninja: execvp");
      }
    } while (false);
    _exit(1);
  } else {  // Child.
    close(pipefd[0]);

    // Write the script file into the stdin of the Python process.
    ssize_t len = write(pipefd[1], kBrowsePy, sizeof(kBrowsePy));
    if (len < (ssize_t)sizeof(kBrowsePy))
      perror("ninja: write");
    close(pipefd[1]);
    exit(0);
  }
}
