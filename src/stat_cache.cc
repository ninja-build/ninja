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

#include "stat_cache.h"

#include "graph.h"
#include "hash_map.h"
#include "includes_normalize.h"
#include "metrics.h"
#include "state.h"
#include "util.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <vector>

namespace {
vector<StringPiece> gPaths;
bool gHavePreCached = false;
}

void StatCache::Init() {
  gPaths.reserve(50000);
}

// static
void StatCache::Inform(StringPiece path) {
#ifdef _WIN32
  if (gHavePreCached)
    return;
  //printf("   inform: %*s\n", path.len_, path.str_);
  StringPiece::const_reverse_iterator at =
      std::find(path.rbegin(), path.rend(), '\\');
  if (at != path.rend())
    gPaths.push_back(StringPiece(path.str_, at.base() - path.str_));
#endif
}

// static
void StatCache::PreCache(State* state) {
#ifdef _WIN32
  METRIC_RECORD("statcache precache");
  string root = ""; // We always want to search the root as well.
  gPaths.push_back(root);
  sort(gPaths.begin(), gPaths.end());
  vector<StringPiece>::const_iterator end = unique(gPaths.begin(), gPaths.end());
  for (vector<StringPiece>::const_iterator i = gPaths.begin(); i != end; ++i) {
    WIN32_FIND_DATA find_data;
    string search_root = IncludesNormalize::ToLower(i->AsString());
    string search = search_root + "*";
    //printf("stating: %s\n", search.c_str());
    HANDLE handle = FindFirstFile(search.c_str(), &find_data);
    if (handle == INVALID_HANDLE_VALUE)
      continue;
    for (;;) {
      if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        string name = search_root +
                      IncludesNormalize::ToLower(find_data.cFileName);
        //string err;
        //if (!CanonicalizePath(&name, &err))
          //continue;
        Node* node = state->LookupNode(name);
        //printf("  cache for %s: %p\n", name.c_str(), node);
        if (node)
          node->set_mtime(FiletimeToTimestamp(find_data.ftLastWriteTime));
      }
      BOOL success = FindNextFile(handle, &find_data);
      if (!success)
        break;
    }
    FindClose(handle);
  }
  gHavePreCached = true;
#endif
}
