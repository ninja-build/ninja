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
  // array, only one row plus one element are used at a time, so this
  // implementation just keeps one vector for the row.  To update one entry,
  // only the entries to the left, top, and top-left are needed.  The left
  // entry is in row[x-1], the top entry is what's in row[x] from the last
  // iteration, and the top-left entry is stored in previous.
  int m = s1.len_;
  int n = s2.len_;

  vector<int> row(n + 1);
  for (int i = 1; i <= n; ++i)
    row[i] = i;

  for (int y = 1; y <= m; ++y) {
    row[0] = y;
    int best_this_row = row[0];

    int previous = y - 1;
    for (int x = 1; x <= n; ++x) {
      int old_row = row[x];
      if (allow_replacements) {
        row[x] = min(previous + (s1.str_[y - 1] == s2.str_[x - 1] ? 0 : 1),
                     min(row[x - 1], row[x]) + 1);
      }
      else {
        if (s1.str_[y - 1] == s2.str_[x - 1])
          row[x] = previous;
        else
          row[x] = min(row[x - 1], row[x]) + 1;
      }
      previous = old_row;
      best_this_row = min(best_this_row, row[x]);
    }

    if (max_edit_distance && best_this_row > max_edit_distance)
      return max_edit_distance + 1;
  }

  return row[n];
}
