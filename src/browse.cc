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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sstream>
#include <vector>

#include "build/browse_py.h"

namespace ninja {

// Log the last error with the provided prefix. This is
// designed to replace usage of perror(prefix) with a system
// that sends the error to our logger class instead of directly
// to stderr.
void LogError(Logger* logger, const char* prefix) {
  std::ostringstream buffer;
  buffer << prefix << ": " << strerror(errno) << std::endl;
  logger->Error(buffer.str());
}

void RunBrowsePython(Logger* logger,
                     const char* ninja_command,
                     const char* input_file,
                     const char* initial_target) {
  // Fork off a Python process and have it run our code via its stdin.
  // (Actually the Python process becomes the parent.)
  int pipefd[2];
  if (pipe(pipefd) < 0) {
    LogError(logger, "ninja: pipe");
    return;
  }

  pid_t pid = fork();
  if (pid < 0) {
    LogError(logger, "ninja: fork");
    return;
  }

  if (pid > 0) {  // Parent.
    close(pipefd[1]);
    do {
      if (dup2(pipefd[0], 0) < 0) {
        LogError(logger, "ninja: dup2");
        break;
      }

      std::vector<const char *> command;
      command.push_back(NINJA_PYTHON);
      command.push_back("-");
      command.push_back("--ninja-command");
      command.push_back(ninja_command);
      command.push_back("-f");
      command.push_back(input_file);
      if (initial_target) {
        command.push_back(initial_target);
      }
      command.push_back(NULL);
      execvp(command[0], (char**)&command[0]);
      if (errno == ENOENT) {
        std::ostringstream buffer;
        buffer << NINJA_PYTHON << " is required for the browse tool";
        logger->Error(buffer.str());
      } else {
        LogError(logger, "ninja: execvp");
      }
    } while (false);
    _exit(1);
  } else {  // Child.
    close(pipefd[0]);

    // Write the script file into the stdin of the Python process.
    ssize_t len = write(pipefd[1], kBrowsePy, sizeof(kBrowsePy));
    if (len < (ssize_t)sizeof(kBrowsePy))
      LogError(logger, "ninja: write");
    close(pipefd[1]);
    exit(0);
  }
}
}  // namespace ninja
