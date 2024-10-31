// Copyright 2024 Google Inc. All Rights Reserved.
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

#include <stddef.h>

#include <memory>
#include <vector>

// A simple bump allocator that gives very fast and tight memory allocation
// for small values. It is primarily intended for StringPiece allocation,
// but all values returned are 8-byte aligned, so you can allocate more
// complex objects on it if you wish.
//
// All pointers returned by Alloc() are valid until the arena is destroyed,
// at which point everything is deallocated all at once. No destructors
// are run.
//
// The arena starts by allocating a single 4 kB block, and then increases by
// 50% every time it needs a new block. This gives O(1) calls to malloc.

struct Arena {
 public:
  static constexpr size_t kAlignTarget = sizeof(uint64_t);

  char* Alloc(size_t num_bytes) {
    num_bytes = (num_bytes + kAlignTarget) & ~kAlignTarget;
    if (static_cast<size_t>(cur_end_ - cur_ptr_) >= num_bytes) {
      char *ret = cur_ptr_;
      cur_ptr_ += num_bytes;
      return ret;
    }

    return AllocSlowPath(num_bytes);
  }

 private:
  struct Block {
    std::unique_ptr<char[]> mem;
  };

  char* AllocSlowPath(size_t num_bytes);

  std::vector<Block> blocks;
  char* cur_ptr_ = nullptr;
  char* cur_end_ = nullptr;
  size_t next_size_ = 4096;
};
