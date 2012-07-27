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

#include <stdio.h>
#include <stdlib.h>

#include "depfile_parser.h"
#include "util.h"
#include "metrics.h"

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("usage: %s <file1> <file2...>\n", argv[0]);
    return 1;
  }

  vector<float> times;
  for (int i = 1; i < argc; ++i) {
    const char* filename = argv[i];

    for (int limit = 1 << 10; limit < (1<<20); limit *= 2) {
      int64_t start = GetTimeMillis();
      for (int rep = 0; rep < limit; ++rep) {
        string buf;
        string err;
        if (ReadFile(filename, &buf, &err) < 0) {
          printf("%s: %s\n", filename, err.c_str());
          return 1;
        }

        DepfileParser parser;
        if (!parser.Parse(&buf, &err)) {
          printf("%s: %s\n", filename, err.c_str());
          return 1;
        }
      }
      int64_t end = GetTimeMillis();

      if (end - start > 100) {
        int delta = (int)(end - start);
        float time = delta*1000 / (float)limit;
        printf("%s: %.1fus\n", filename, time);
        times.push_back(time);
        break;
      }
    }
  }

  if (!times.empty()) {
    float min = times[0];
    float max = times[0];
    float total = 0;
    for (size_t i = 0; i < times.size(); ++i) {
      total += times[i];
      if (times[i] < min)
        min = times[i];
      else if (times[i] > max)
        max = times[i];
    }

    printf("min %.1fus  max %.1fus  avg %.1fus\n",
           min, max, total / times.size());
  }

  return 0;
}
