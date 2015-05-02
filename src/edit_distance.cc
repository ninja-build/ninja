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

#include "edit_distance.h"

#include <algorithm>
#include <vector>

int EditDistance(const StringPiece& s1,
                 const StringPiece& s2,
                 bool allow_replacements,
                 int max_edit_distance) {
  // The algorithm implemented below is the "classic"
  // dynamic-programming algorithm for computing the Levenshtein
  // distance, which is described here:
  //
  //   http://en.wikipedia.org/wiki/Levenshtein_distance
  //
  // Although the algorithm is typically described using an m x n
  // array, only two rows are used at a time, so this implementation
  // just keeps two separate vectors for those two rows.
  int m = s1.len_;
  int n = s2.len_;

  vector<int> previous(n + 1);
  vector<int> current(n + 1);

  for (int i = 0; i <= n; ++i)
    previous[i] = i;

  for (int y = 1; y <= m; ++y) {
    current[0] = y;
    int best_this_row = current[0];

    for (int x = 1; x <= n; ++x) {
      if (allow_replacements) {
        current[x] = min(previous[x-1] + (s1.str_[y-1] == s2.str_[x-1] ? 0 : 1),
                         min(current[x-1], previous[x])+1);
      }
      else {
        if (s1.str_[y-1] == s2.str_[x-1])
          current[x] = previous[x-1];
        else
          current[x] = min(current[x-1], previous[x]) + 1;
      }
      best_this_row = min(best_this_row, current[x]);
    }

    if (max_edit_distance && best_this_row > max_edit_distance)
      return max_edit_distance + 1;

    current.swap(previous);
  }

  return previous[n];
}
