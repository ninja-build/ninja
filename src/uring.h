// Copyright 2020 Max Kellermann. All Rights Reserved.
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

#ifndef NINJA_URING_H_
#define NINJA_URING_H_

#include "timestamp.h"

#include <map>

#include <liburing.h>
#include <sys/stat.h>

/// Helper class for submitting bulk statx() calls to the Linux kernel
/// via io_uring
class BulkStat {
  typedef void (*Callback)(TimeStamp t, const char* error, void* data);

public:
  BulkStat();

  ~BulkStat() {
    Close();
  }

  /// Is io_uring/statx available?  This requires Linux 5.6 or newer.
  bool IsAvailable() const {
    return ring.ring_fd >= 0;
  }

  void SetCallback(Callback callback) {
    callback_ = callback;
  }

  /// Queue a statx() call on the given path.  The callback will be
  /// invoked later with the given "data" pointer.
  void Queue(const char* path, void* data);

  /// Wait for all queued operations to complete.  After returning,
  /// all pending callbacks have been invoked.
  void Wait();

private:
  void FailAll(const char* error);

  void Close();

  Callback callback_;

  struct io_uring ring;

  typedef std::map<void* , struct statx> PendingMap;
  PendingMap pending;

  PendingMap::size_type n_pending;
};

extern BulkStat *global_bulk_stat;

#endif  // NINJA_URING_H_
