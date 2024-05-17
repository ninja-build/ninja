// Copyright 2024 Google Inc. All Rights Reserved.
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "elide_middle.h"
#include "metrics.h"

static const char* kTestInputs[] = {
  "01234567890123456789",
  "012345\x1B[0;35m67890123456789",
  "abcd\x1b[1;31mefg\x1b[0mhlkmnopqrstuvwxyz",
};

int main() {
  std::vector<int> times;

  int64_t kMaxTimeMillis = 5 * 1000;
  int64_t base_time = GetTimeMillis();

  const int kRuns = 100;
  for (int j = 0; j < kRuns; ++j) {
    int64_t start = GetTimeMillis();
    if (start >= base_time + kMaxTimeMillis)
      break;

    const int kNumRepetitions = 2000;
    for (int count = kNumRepetitions; count > 0; --count) {
      for (const char* input : kTestInputs) {
        size_t input_len = ::strlen(input);
        for (size_t max_width = input_len; max_width > 0; --max_width) {
          std::string str(input, input_len);
          ElideMiddleInPlace(str, max_width);
        }
      }
    }

    int delta = (int)(GetTimeMillis() - start);
    times.push_back(delta);
  }

  int min = times[0];
  int max = times[0];
  float total = 0;
  for (size_t i = 0; i < times.size(); ++i) {
    total += times[i];
    if (times[i] < min)
      min = times[i];
    else if (times[i] > max)
      max = times[i];
  }

  printf("min %dms  max %dms  avg %.1fms\n", min, max, total / times.size());

  return 0;
}
