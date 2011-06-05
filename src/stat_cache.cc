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

#include "stat_cache.h"

#include <stdio.h>

#include "graph.h"

FileStat* StatCache::GetFile(const std::string& path) {
  Paths::iterator i = paths_.find(path);
  if (i != paths_.end())
    return i->second;
  FileStat* file = new FileStat(path);
  paths_[path] = file;
  return file;
}

void StatCache::Dump() {
  for (Paths::iterator i = paths_.begin(); i != paths_.end(); ++i) {
    FileStat* file = i->second;
    printf("%s %s\n",
           file->path_.c_str(),
           file->status_known() ? (file->node_->dirty_ ? "dirty" : "clean")
                                : "unknown");
  }
}
