// Copyright 2026 Ninja Authors
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
#include "hash_cache.h"

#include <cassert>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "disk_interface.h"

HashCache::HashCache() {
  // Read .ninja_hashes from disk if it exists
  std::ifstream in(".ninja_hashes");
  if (!in) {
    return;  // File doesn't exist yet, start with empty cache
  }

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty())
      continue;
    std::istringstream iss(line);
    uint64_t hash;
    TimeStamp mtime;
    if (!(iss >> std::hex >> hash >> std::dec >> mtime))
      continue;
    std::string path;
    std::getline(iss >> std::ws, path);
    if (path.empty())
      continue;
    files_[path] = FileInfo{ .hash = hash, .mtime = mtime };
  }
}

HashCache::~HashCache() {
  // Save .ninja_hashes to disk
  std::ofstream out(".ninja_hashes");
  if (!out) {
    return;  // If we can't write, just exit
  }

  for (const auto& [path, info] : files_) {
    out << std::hex << info.hash << " " << std::dec << info.mtime << " " << path
        << "\n";
  }
}

std::optional<TimeStamp> HashCache::Stat(const DiskInterface& disk_interface,
                                         const std::string& path) {
  auto time_stamp = disk_interface.StatImpl(path);
  if (!time_stamp.has_value()) {
    return std::nullopt;
  }
  auto it = files_.find(path);
  if (it == files_.end()) {
    auto hash = FNV1a_64_Hash(path);
    if (!hash) {
      return *time_stamp;  // File is empty or directory, don't cache this
    }
    files_.emplace(path, FileInfo{ .hash = *hash, .mtime = *time_stamp });
  } else if (*time_stamp != it->second.mtime) {
    assert(*time_stamp > it->second.mtime);  // mtime should only increase
    auto hash = FNV1a_64_Hash(path);
    if (it->second.hash == hash) {
      // std::cerr << "DEBUG HashCache: path=" << path
      //           << " unchanged, mtime=" << *time_stamp << "\n";
      return it->second.mtime;  // File unchanged, return cached mtime
    }
    if (hash) {
      // File changed, update cache
      it->second.hash = hash.value();
      it->second.mtime = *time_stamp;
    } else {
      // File is now empty or a directory, remove from cache
      files_.erase(it);
    }
  }
  return *time_stamp;
}

std::optional<uint64_t> HashCache::FNV1a_64_Hash(
    const std::string& path) const {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    return std::nullopt;
  }
  const uint64_t fnv_prime = 1099511628211ULL;
  uint64_t hash = 14695981039346656037ULL;
  char buffer[8192];
  size_t count;
  while ((count = fread(buffer, 1, sizeof(buffer), f)) > 0) {
    for (size_t i = 0; i < count; ++i) {
      hash ^= static_cast<unsigned char>(buffer[i]);
      hash *= fnv_prime;
    }
  }
  fclose(f);
  if (hash == 14695981039346656037ULL) {
    return std::nullopt;  // File is empty or directory, don't cache this
  }
  return hash;
}
