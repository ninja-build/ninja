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

#include <windows.h>

#include <cassert>

#include "util.h"

void Jobserver::Init() {
  assert(sem_ == INVALID_HANDLE_VALUE);

  if (!ParseJobserverAuth("sem")) {
    return;
  }

  const char* name = jobserver_name_.c_str();

  sem_ = OpenSemaphore(SYNCHRONIZE|SEMAPHORE_MODIFY_STATE, false, name);
  if (sem_ == nullptr) {
    Win32Fatal("OpenSemaphore", name);
  }

  Info("using jobserver: %s", name);
}

Jobserver::~Jobserver() {
  assert(token_count_ == 0);

  if (sem_ != INVALID_HANDLE_VALUE) {
    CloseHandle(sem_);
  }
}

bool Jobserver::Enabled() const {
  return sem_ != INVALID_HANDLE_VALUE;
}

bool Jobserver::AcquireToken() {
  return WaitForSingleObject(sem_, 0) == WAIT_OBJECT_0;
}

void Jobserver::ReleaseToken() {
  if (!ReleaseSemaphore(sem_, 1, nullptr)) {
    Win32Fatal("ReleaseSemaphore");
  }
}
