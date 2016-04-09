// Copyright 2014 Matthias Maennich (matthias@maennich.net).
// All Rights Reserved.
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

#ifndef NINJA_HASH_LOG_H_
#define NINJA_HASH_LOG_H_

#include <map>
#include <vector>
#include <string>

#include <stdio.h>

#ifdef _WIN32
#include "win32port.h"
#else
#include <stdint.h>
#endif

#include "disk_interface.h"

extern const char kHashLogFileName[];

struct Node;
struct Edge;

struct HashLog {
  HashLog(const std::string& filename, DiskInterface* disk_interface)
    : filename_(filename)
    , file_(NULL)
    , disk_interface_(disk_interface)
    , total_values_(0)
    , hash_map_()
    , changed_files_() {}
  ~HashLog();

  typedef DiskInterface::hash_t hash_t;

  /// the mapped type of the hash map
  ///
  /// it consists of
  ///   hash_   the file hash
  ///   mtime_  the modification time at the time of hashing
  struct mapped_t {
    hash_t    hash_;
    TimeStamp mtime_;
    mapped_t(const hash_t hash, const TimeStamp mtime)
        : hash_(hash), mtime_(mtime) {}
  };

  /// the hashlog contains different variants of hashes
  ///
  /// SOURCE  -  used for source files (inputs hashes)
  /// TARGET  -  computed hash of inputs (for targets)
  enum hash_variant {
      UNDEFINED = 0,
      SOURCE = 1,
      TARGET = 2
  };

  /// the hash map key type
  struct key_t {
    hash_variant variant_;
    std::string val_;
    key_t(const hash_variant variant, const std::string& val)
        : variant_(variant), val_(val) {}
  };

  friend bool operator==(const key_t& lhs, const key_t& rhs) {
      return lhs.variant_ == rhs.variant_ && lhs.val_ == rhs.val_;
  }

  friend bool operator!=(const key_t& lhs, const key_t& rhs) {
      return !(lhs == rhs);
  }

  friend bool operator<(const key_t& lhs, const key_t& rhs) {
      if (lhs.variant_ == rhs.variant_) {
          return lhs.val_ < rhs.val_;
      } else {
         return lhs.variant_ < rhs.variant_;
      }
  }

  /// the hash map type
  typedef std::map<key_t, mapped_t> map_t;

  /// update the hash of a node (if necessary, or always if forced)
  ///
  /// returns true if the hash has been updated, else false
  /// sets *err in case of any errors
  bool   UpdateHash(Node* node, hash_variant variant,
                    std::string* err, bool force = false,
                    hash_t* result = NULL);

  /// get the recorded hash for a node (mostly used by tests)
  ///
  /// sets *err in case of any errors
  hash_t GetHash(Node* node, hash_variant variant, std::string* err);

  /// get whether the hash of a file has changed since the last recording
  ///
  /// sets *err in case of any errors
  bool   HashChanged(Node* node, hash_variant variant, std::string* err);

  /// check whether an edge and its inputs have changed hash-wise
  ///
  /// sets *err in case of any errors
  bool   EdgeChanged(const Edge* edge, std::string* err);

  /// persist hashes (source and target) for a finished edge
  ///
  /// sets *err in case of any errors
  void   EdgeFinished(const Edge* edge, std::string* err);

  /// recompact the hash log to reduce it to minimum size
  ///
  /// returns true if the log had to be recompacted
  bool   Recompact(std::string* err, bool force = false);

  /// close the hash log
  ///
  /// returns true if the log had to be closed
  bool   Close();

 private:
  /// load the hash log
  ///
  /// returns false and sets *err in case of any errors
  bool Load(std::string* err);

  /// put a new hash (along with mapped data)
  /// to the internal hash map and to the file
  ///
  /// returns true if the hash has been put into the log
  /// not returning true does not necessarily indicate an error
  ///
  /// errors are indicated by setting *err
  bool PutHash(const std::string& path, hash_t hash,
               TimeStamp mtime, hash_variant variant,
               string* err);

  /// the hash log file to persist the log
  const std::string filename_;

  /// the file object
  FILE* file_;

  /// the backend interface for file operations
  DiskInterface* disk_interface_;

  /// the counter of total values in the persisted hash log
  /// this is used to decide on recompacting
  uint64_t total_values_;

  /// the internal hash map as a representation of the persisted hash log
  map_t hash_map_;

  /// files detected as changed during this run (no need to determine again)
  std::map<Node*,bool> changed_files_;
};

#endif //NINJA_HASH_LOG_H_
