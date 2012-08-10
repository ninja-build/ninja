// Copyright 2012 Google Inc. All Rights Reserved.
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

#include "build_log.h"

#include <algorithm>
using namespace std;

#include <time.h>

int random(int low, int high) {
  return int(low + (rand() / double(RAND_MAX)) * (high - low) + 0.5);
}

void RandomCommand(char** s) {
  int len = random(5, 100);
  *s = new char[len];
  for (int i = 0; i < len; ++i)
    (*s)[i] = (char)random(32, 127);
}

int main() {
  const int N = 20 * 1000 * 1000;

  // Leak these, else 10% of the runtime is spent destroying strings.
  char** commands = new char*[N];
  pair<uint64_t, int>* hashes = new pair<uint64_t, int>[N];

  srand((int)time(NULL));

  for (int i = 0; i < N; ++i) {
    RandomCommand(&commands[i]);
    hashes[i] = make_pair(BuildLog::LogEntry::HashCommand(commands[i]), i);
  }

  sort(hashes, hashes + N);

  int num_collisions = 0;
  for (int i = 1; i < N; ++i) {
    if (hashes[i - 1].first == hashes[i].first) {
      if (strcmp(commands[hashes[i - 1].second],
                 commands[hashes[i].second]) != 0) {
        printf("collision!\n  string 1: '%s'\n  string 2: '%s'\n",
               commands[hashes[i - 1].second],
               commands[hashes[i].second]);
        num_collisions++;
      }
    }
  }
  printf("\n\n%d collisions after %d runs\n", num_collisions, N);
}
