#include "util.h"

const char kPath[] =
    "../../third_party/WebKit/Source/WebCore/"
    "platform/leveldb/LevelDBWriteBatch.cpp";

int main() {
  vector<int> times;
  string err;

  char buf[200];
  int len = strlen(kPath);
  strcpy(buf, kPath);

  for (int j = 0; j < 5; ++j) {
    const int kNumRepetitions = 2000000;
    int64_t start = GetTimeMillis();
    for (int i = 0; i < kNumRepetitions; ++i) {
      CanonicalizePath(buf, &len, &err);
    }
    int delta = (int)(GetTimeMillis() - start);
    times.push_back(delta);
  }

  int min = times[0];
  int max = times[0];
  float total = 0.0;
  for (size_t i = 0; i < times.size(); ++i) {
    total += static_cast<float>(times[i]);
    if (times[i] < min)
      min = times[i];
    else if (times[i] > max)
      max = times[i];
  }

  printf("min %dms  max %dms  avg %.1fms\n",
         min, max, total / static_cast<float>(times.size()));
}
