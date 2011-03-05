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

#include <stdio.h>
#include <unistd.h>

#include "ninja.h"

// Import browse.py as binary data.
asm(
".data\n"
"browse_data_begin:\n"
".incbin \"src/browse.py\"\n"
"browse_data_end:\n"
);
// Declare the symbols defined above.
extern const char browse_data_begin[];
extern const char browse_data_end[];

void RunBrowsePython(State* state, const char* ninja_command) {
  // Fork off a Python process and have it run our code via its stdin.
  // (Actually the Python process becomes the parent.)
  int pipefd[2];
  if (pipe(pipefd) < 0) {
    perror("pipe");
    return;
  }

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return;
  }

  if (pid > 0) {  // Parent.
    close(pipefd[1]);
    do {
      if (dup2(pipefd[0], 0) < 0) {
        perror("dup2");
        break;
      }

      // exec Python, telling it to run the program from stdin.
      const char* command[] = {
        "python", "-", ninja_command, NULL
      };
      execvp(command[0], (char**)command);
      perror("execvp");
    } while (false);
    _exit(1);
  } else {  // Child.
    close(pipefd[0]);

    // Write the script file into the stdin of the Python process.
    const int browse_data_len = browse_data_end - browse_data_begin;
    int len = write(pipefd[1], browse_data_begin, browse_data_len);
    if (len < browse_data_len)
      perror("write");
    close(pipefd[1]);
    exit(0);
  }
}
