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
  // Create a temporary file, dump the Python code into it, and
  // delete the file, keeping our open handle to it.
  char tmpl[] = "browsepy-XXXXXX";
  int fd = mkstemp(tmpl);
  unlink(tmpl);
  const int browse_data_len = browse_data_end - browse_data_begin;
  int len = write(fd, browse_data_begin, browse_data_len);
  if (len < browse_data_len) {
    perror("write");
    return;
  }

  // exec Python, telling it to use our script file.
  const char* command[] = {
    "python", "/proc/self/fd/3", ninja_command, NULL
  };
  execvp(command[0], (char**)command);

  // If we get here, the exec failed.
  printf("ERROR: Failed to spawn python for graph browsing, aborting.\n");
}
