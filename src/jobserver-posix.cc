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

#include "jobserver.h"

#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <cstring>

#include "util.h"

void Jobserver::Init() {
  assert(fd_ < 0);

  if (!ParseJobserverAuth("fifo")) {
    return;
  }

  const char* jobserver = jobserver_name_.c_str();

  fd_ = open(jobserver, O_NONBLOCK | O_CLOEXEC | O_RDWR);
  if (fd_ < 0) {
    Fatal("failed to open jobserver: %s: %s", jobserver, strerror(errno));
  }

  Info("using jobserver: %s", jobserver);
}

Jobserver::~Jobserver() {
  assert(token_count_ == 0);

  if (fd_ >= 0) {
    close(fd_);
  }
}

bool Jobserver::Enabled() const {
  return fd_ >= 0;
}

bool Jobserver::AcquireToken() {
  char token;
  int res = read(fd_, &token, 1);
  if (res < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    Fatal("failed to read jobserver token: %s", strerror(errno));
  }

  return res > 0;
}

void Jobserver::ReleaseToken() {
  char token = '+';
  int res = write(fd_, &token, 1);
  if (res != 1) {
    Fatal("failed to write token: %s: %s", jobserver_name_.c_str(),
          strerror(errno));
  }
}
