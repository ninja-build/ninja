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

#include "uring.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

BulkStat *global_bulk_stat;

BulkStat::BulkStat() : callback_(NULL), n_pending(0) {
  if (io_uring_queue_init(1024, &ring, 0) < 0) {
    ring.ring_fd = -1;
    return;
  }

  struct io_uring_probe* probe = io_uring_get_probe_ring(&ring);
  if (!probe) {
    Close();
    return;
  }

  bool supported = io_uring_opcode_supported(probe, IORING_OP_STATX);
  free(probe);
  if (!supported) {
    Close();
    return;
  }
}

void BulkStat::Queue(const char* path, void* data) {
  assert(callback_);

  if (!IsAvailable()) {
    callback_(-1, "io_uring is not available", data);
    return;
  }

  struct io_uring_sqe* s = io_uring_get_sqe(&ring);
  if (!s) {
    Wait();

    s = io_uring_get_sqe(&ring);
    if (!s) {
      callback_(-1, "io_uring_get_sqe() failed", data);
      return;
    }
  }

  std::pair<void*, struct statx> value;
  value.first = data;
  PendingMap::iterator i = pending.insert(value).first;

  io_uring_prep_statx(s, AT_FDCWD, path, AT_STATX_FORCE_SYNC,
                      STATX_MTIME, &i->second);
  io_uring_sqe_set_data(s, data);

  ++n_pending;
}

void BulkStat::Wait() {
  assert(callback_);

  if (pending.empty())
    return;

  assert(IsAvailable());

  int error = io_uring_submit_and_wait(&ring, n_pending);
  if (error < 0) {
    // something went terribly wrong, shouldn't happen
    FailAll(strerror(-error));
    Close();
    return;
  }

  while (!pending.empty()) {
    struct io_uring_cqe *cqe;
    error = io_uring_wait_cqe(&ring, &cqe);
    if (error < 0) {
      if (error == -EAGAIN)
        break;

      // something went terribly wrong, shouldn't happen
      FailAll(strerror(-error));
      Close();
      return;
    }

    void* data = io_uring_cqe_get_data(cqe);
    assert(data);
    PendingMap::const_iterator i = pending.find(data);
    assert(i != pending.end());

    if (cqe->res == 0) {
      TimeStamp t = 0;
      if (i->second.stx_mask & STATX_MTIME) {
        t = i->second.stx_mtime.tv_sec;
        if (t == 0)
          // Some users (Flatpak) set mtime to 0, this should be
          // harmless and avoids conflicting with our return
          // value of 0 meaning that it doesn't exist.
          t = 1;
      }

      callback_(t, NULL, data);
    } else {
      callback_(-1, strerror(-cqe->res), data);
    }

    pending.erase(i);
    --n_pending;
    io_uring_cqe_seen(&ring, cqe);
  }
}

void BulkStat::FailAll(const char* error) {
  assert(callback_);

  for (PendingMap::const_iterator i = pending.begin(); i != pending.end(); ++i)
    callback_(-1, error, i->first);

  pending.clear();
}

void BulkStat::Close() {
  if (ring.ring_fd >= 0) {
    io_uring_queue_exit(&ring);
    ring.ring_fd = -1;
  }
}
