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
#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "timestamp.h"

struct DiskInterface;

/// File .ninja_hashes that contains one line per file:
///   <hash_hex> <mtime> <path>
///
/// When calling Stat(path) and the path is found in the cache and the hash of
/// the actual file matches, it will return the cached mtime.
class HashCache {
 public:
  /// Reads .ninja_hashes from disk
  HashCache();

  /// Saves .ninja_hashes to disk
  ~HashCache();

  std::optional<TimeStamp> Stat(const DiskInterface& disk_interface, const std::string& path);

 private:
  std::optional<uint64_t> FNV1a_64_Hash(const std::string& path) const;

  struct FileInfo {
    uint64_t hash;
    TimeStamp mtime;
  };
  std::unordered_map<std::string, FileInfo> files_;
};
