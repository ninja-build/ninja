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

#include <assert.h>
#include <windows.h>

#include <atomic>

#include "jobserver.h"
#include "util.h"

namespace {

// Implementation of Jobserver::Client for Win32 systems.
// At the moment, only the semaphore scheme is supported,
// even when running under Cygwin which could support the
// pipe version, in theory.
class Win32JobserverClient : public Jobserver::Client {
 public:
  virtual ~Win32JobserverClient() {
    StopWatching();
    // NOTE: OpenSemaphore() returns NULL on failure.
    if (IsValid()) {
      ::CloseHandle(handle_);
    }
  }

  Jobserver::Slot TryAcquire() override {
    if (IsValid()) {
      if (has_implicit_slot_) {
        has_implicit_slot_ = false;
        return Jobserver::Slot::CreateImplicit();
      }

      // A slot acquired by the watching thread comes first.
      if (watch_state_ == kWatchHeld) {
        watch_state_ = kWatchIdle;
        return Jobserver::Slot::CreateExplicit(1);
      }

      DWORD ret = ::WaitForSingleObject(handle_, 0);
      if (ret == WAIT_OBJECT_0) {
        // Hard-code value 1 for the explicit slot value.
        return Jobserver::Slot::CreateExplicit(1);
      }
    }
    return Jobserver::Slot();
  }

  void Release(Jobserver::Slot slot) override {
    if (!slot.IsValid())
      return;

    if (slot.IsImplicit()) {
      assert(!has_implicit_slot_ && "Implicit slot cannot be released twice!");
      has_implicit_slot_ = true;
      return;
    }

    // Nothing can be done in case of error here.
    (void)::ReleaseSemaphore(handle_, 1, NULL);
  }

  void WatchForSlot(void (*notify)()) override {
    if (!IsValid() || watch_state_ != kWatchIdle)
      return;
    if (!cancel_event_) {
      cancel_event_ = ::CreateEventA(NULL, TRUE, FALSE, NULL);
      if (!cancel_event_)
        return;
    }
    JoinThread();
    notify_ = notify;
    watch_state_ = kWatchWaiting;
    thread_ = ::CreateThread(NULL, 0, WatchThreadProc, this, 0, NULL);
    if (!thread_)
      watch_state_ = kWatchIdle;
  }

  void StopWatching() override {
    if (watch_state_ == kWatchWaiting)
      ::SetEvent(cancel_event_);
    JoinThread();
    if (cancel_event_) {
      ::CloseHandle(cancel_event_);
      cancel_event_ = NULL;
    }
    if (watch_state_ == kWatchHeld) {
      watch_state_ = kWatchIdle;
      (void)::ReleaseSemaphore(handle_, 1, NULL);
    }
  }

  bool InitWithSemaphore(const std::string& name, std::string* error) {
    handle_ = ::OpenSemaphoreA(SYNCHRONIZE | SEMAPHORE_MODIFY_STATE, FALSE,
                               name.c_str());
    if (handle_ == NULL) {
      *error = "Error opening semaphore: " + GetLastErrorString();
      return false;
    }
    return true;
  }

 protected:
  bool IsValid() const {
    // NOTE: OpenSemaphore() returns NULL on failure, not INVALID_HANDLE_VALUE.
    return handle_ != NULL;
  }

  // Wait for a slot (or for cancel_event_) in the background, and wake up
  // the main thread with notify_ once a slot has been acquired.
  static DWORD WINAPI WatchThreadProc(LPVOID param) {
    Win32JobserverClient* self = static_cast<Win32JobserverClient*>(param);
    HANDLE handles[2] = { self->handle_, self->cancel_event_ };
    if (::WaitForMultipleObjects(2, handles, FALSE, INFINITE) ==
        WAIT_OBJECT_0) {
      self->watch_state_ = kWatchHeld;
      self->notify_();
    } else {
      self->watch_state_ = kWatchIdle;
    }
    return 0;
  }

  void JoinThread() {
    if (thread_) {
      ::WaitForSingleObject(thread_, INFINITE);
      ::CloseHandle(thread_);
      thread_ = NULL;
    }
  }

  enum { kWatchIdle, kWatchWaiting, kWatchHeld };

  // State of the watching thread: kWatchHeld means that it acquired a slot
  // that TryAcquire() has not handed out yet.
  std::atomic<int> watch_state_{kWatchIdle};
  HANDLE thread_ = NULL;
  HANDLE cancel_event_ = NULL;
  void (*notify_)() = NULL;

  // Set to true if the implicit slot has not been acquired yet.
  bool has_implicit_slot_ = true;

  // Semaphore handle. NULL means not in use.
  HANDLE handle_ = NULL;
};

}  // namespace

// static
std::unique_ptr<Jobserver::Client> Jobserver::Client::Create(
    const Jobserver::Config& config, std::string* error) {
  bool success = false;
  auto client =
      std::unique_ptr<Win32JobserverClient>(new Win32JobserverClient());
  if (config.mode == Jobserver::Config::kModeWin32Semaphore) {
    success = client->InitWithSemaphore(config.path, error);
  } else {
    *error = "Unsupported jobserver mode";
  }
  if (!success)
    client.reset();
  return client;
}
