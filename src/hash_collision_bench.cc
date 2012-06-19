#include "build_log.h"
#include <algorithm> // use sort

int random(int low, int high) {
  return int(low + (rand() / double(RAND_MAX)) * (high - low) + 0.5);
}

void RandomCommand(char** s) {
  int len = random(5, 100);
  *s = new char[len];
  for (int i = 0; i < len; ++i)
    (*s)[i] = static_cast<char>(random(32, 127));
}

int main() {
  const int N = 20 * 1000 * 1000;

  // Leak these, else 10% of the runtime is spent destroying strings.
  char** commands = new char*[N];
  pair<uint64_t, int>* hashes = new pair<uint64_t, int>[N];

  srand(time(NULL));

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
