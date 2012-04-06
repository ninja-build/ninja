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
#include "metrics.h"
#include "state.h"
#include "util.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <vector>

// TODO could probably be StringPiece, they all come from Nodes
static vector<string> gPaths;
bool gHavePreCached = false;

void StatCache::Init() {
  gPaths.reserve(50000);
}

// static
void StatCache::Inform(const string& path) {
#ifdef _WIN32
  if (gHavePreCached)
    return;
  size_t i = path.find_last_of("\\/");
  if (i != string::npos)
    gPaths.push_back(string(path.data(), i));
#endif
}

// static
void StatCache::PreCache(State* state) {
#ifdef _WIN32
  METRIC_RECORD("statcache precache");
  sort(gPaths.begin(), gPaths.end());
  vector<string>::const_iterator end = unique(gPaths.begin(), gPaths.end());
  for (vector<string>::const_iterator i = gPaths.begin(); i != end; ++i) {
    //printf("prestat %s\n", i->c_str());
    WIN32_FIND_DATA find_data;
    string search = *i + string("\\*");
    HANDLE handle = FindFirstFile(search.c_str(), &find_data);
    if (handle == INVALID_HANDLE_VALUE)
      continue;
    for (;;) {
      if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        string name = *i + string("\\") + string(find_data.cFileName), err;
        //if (!CanonicalizePath(&name, &err))
          //continue;
        Node* node = state->LookupNode(name);
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
