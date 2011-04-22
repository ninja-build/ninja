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

#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <vector>

void Fatal(const char* msg, ...) {
  va_list ap;
  fprintf(stderr, "ninja: FATAL: ");
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  exit(1);
}

void Error(const char* msg, ...) {
  va_list ap;
  fprintf(stderr, "ninja: error: ");
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fprintf(stderr, "\n");
}

bool CanonicalizePath(std::string* path, std::string* err) {
  // Try to fast-path out the common case.
  if (path->find("/.") == std::string::npos &&
      path->find("./") == std::string::npos &&
      path->find("//") == std::string::npos) {
    return true;
  }

  std::string inpath = *path;
  std::vector<const char*> parts;
  for (std::string::size_type start = 0; start < inpath.size(); ++start) {
    std::string::size_type end = inpath.find('/', start);
    if (end == std::string::npos)
      end = inpath.size();
    else
      inpath[end] = 0;
    if (end > start)
      parts.push_back(inpath.data() + start);
    start = end;
  }

  std::vector<const char*>::iterator i = parts.begin();
  while (i != parts.end()) {
    const char* part = *i;
    if (part[0] == '.') {
      if (part[1] == 0) {
        // "."; strip.
        parts.erase(i);
        continue;
      } else if (part[1] == '.' && part[2] == 0) {
        // ".."; go up one.
        if (i == parts.begin()) {
          *err = "can't canonicalize path '" + *path + "' that reaches "
            "above its directory";
          return false;
        }
        --i;
        parts.erase(i, i + 2);
        continue;
      }
    }
    ++i;
  }
  path->clear();

  for (i = parts.begin(); i != parts.end(); ++i) {
    if (!path->empty())
      path->push_back('/');
    path->append(*i);
  }

  return true;
}
