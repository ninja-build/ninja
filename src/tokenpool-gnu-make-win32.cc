// Copyright 2018 Google Inc. All Rights Reserved.
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

#include "tokenpool-gnu-make.h"

// Always include this first.
// Otherwise the other system headers don't work correctly under Win32
#include <windows.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

// TokenPool implementation for GNU make jobserver - Win32 implementation
// (https://www.gnu.org/software/make/manual/html_node/Windows-Jobserver.html)
struct GNUmakeTokenPoolWin32 : public GNUmakeTokenPool {
  GNUmakeTokenPoolWin32();
  virtual ~GNUmakeTokenPoolWin32();

  virtual void WaitForTokenAvailability(HANDLE ioport);
  virtual bool TokenIsAvailable(ULONG_PTR key);

  virtual const char* GetEnv(const char* name);
  virtual bool ParseAuth(const char* jobserver);
  virtual bool AcquireToken();
  virtual bool ReturnToken();

 private:
  // Semaphore for GNU make jobserver protocol
  HANDLE semaphore_jobserver_;
  // Semaphore Child -> Parent
  // - child releases it before entering wait on jobserver semaphore
  // - parent blocks on it to know when child enters wait
  HANDLE semaphore_enter_wait_;
  // Semaphore Parent -> Child
  // - parent releases it to allow child to restart loop
  // - child blocks on it to know when to restart loop
  HANDLE semaphore_restart_;
  // set to false if child should exit loop and terminate thread
  bool running_;
  // child thread
  HANDLE child_;
  // I/O completion port from SubprocessSet
  HANDLE ioport_;


  DWORD SemaphoreThread();
  void ReleaseSemaphore(HANDLE semaphore);
  void WaitForObject(HANDLE object);
  static DWORD WINAPI SemaphoreThreadWrapper(LPVOID param);
  static void NoopAPCFunc(ULONG_PTR param);
};

GNUmakeTokenPoolWin32::GNUmakeTokenPoolWin32() : semaphore_jobserver_(NULL),
                                                 semaphore_enter_wait_(NULL),
                                                 semaphore_restart_(NULL),
                                                 running_(false),
                                                 child_(NULL),
                                                 ioport_(NULL) {
}

GNUmakeTokenPoolWin32::~GNUmakeTokenPoolWin32() {
  Clear();
  CloseHandle(semaphore_jobserver_);
  semaphore_jobserver_ = NULL;

  if (child_) {
    // tell child thread to exit
    running_ = false;
    ReleaseSemaphore(semaphore_restart_);

    // wait for child thread to exit
    WaitForObject(child_);
    CloseHandle(child_);
    child_ = NULL;
  }

  if (semaphore_restart_) {
    CloseHandle(semaphore_restart_);
    semaphore_restart_ = NULL;
  }

  if (semaphore_enter_wait_) {
    CloseHandle(semaphore_enter_wait_);
    semaphore_enter_wait_ = NULL;
  }
}

const char* GNUmakeTokenPoolWin32::GetEnv(const char* name) {
  // getenv() does not work correctly together with tokenpool_tests.cc
  static char buffer[MAX_PATH + 1];
  if (GetEnvironmentVariable(name, buffer, sizeof(buffer)) == 0)
    return NULL;
  return buffer;
}

bool GNUmakeTokenPoolWin32::ParseAuth(const char* jobserver) {
  // match "--jobserver-auth=gmake_semaphore_<INTEGER>..."
  const char* start = strchr(jobserver, '=');
  if (start) {
    const char* end = start;
    unsigned int len;
    char c, *auth;

    while ((c = *++end) != '\0')
      if (!(isalnum(c) || (c == '_')))
        break;
    len = end - start; // includes string terminator in count

    if ((len > 1) && ((auth = (char*)malloc(len)) != NULL)) {
      strncpy(auth, start + 1, len - 1);
      auth[len - 1] = '\0';

      if ((semaphore_jobserver_ =
           OpenSemaphore(SEMAPHORE_ALL_ACCESS, /* Semaphore access setting */
                         FALSE,                /* Child processes DON'T inherit */
                         auth                  /* Semaphore name */
                        )) != NULL) {
        free(auth);
        return true;
      }

      free(auth);
    }
  }

  return false;
}

bool GNUmakeTokenPoolWin32::AcquireToken() {
  return WaitForSingleObject(semaphore_jobserver_, 0) == WAIT_OBJECT_0;
}

bool GNUmakeTokenPoolWin32::ReturnToken() {
  ReleaseSemaphore(semaphore_jobserver_);
  return true;
}

DWORD GNUmakeTokenPoolWin32::SemaphoreThread() {
  while (running_) {
    // indicate to parent that we are entering wait
    ReleaseSemaphore(semaphore_enter_wait_);

    // alertable wait forever on token semaphore
    if (WaitForSingleObjectEx(semaphore_jobserver_, INFINITE, TRUE) == WAIT_OBJECT_0) {
      // release token again for AcquireToken()
      ReleaseSemaphore(semaphore_jobserver_);

      // indicate to parent on ioport that a token might be available
      if (!PostQueuedCompletionStatus(ioport_, 0, (ULONG_PTR) this, NULL))
        Win32Fatal("PostQueuedCompletionStatus");
    }

    // wait for parent to allow loop restart
    WaitForObject(semaphore_restart_);
    // semaphore is now in nonsignaled state again for next run...
  }

  return 0;
}

DWORD WINAPI GNUmakeTokenPoolWin32::SemaphoreThreadWrapper(LPVOID param) {
  GNUmakeTokenPoolWin32* This = (GNUmakeTokenPoolWin32*) param;
  return This->SemaphoreThread();
}

void GNUmakeTokenPoolWin32::NoopAPCFunc(ULONG_PTR param) {
}

void GNUmakeTokenPoolWin32::WaitForTokenAvailability(HANDLE ioport) {
  if (child_ == NULL) {
    // first invocation
    //
    // subprocess-win32.cc uses I/O completion port (IOCP) which can't be
    // used as a waitable object. Therefore we can't use WaitMultipleObjects()
    // to wait on the IOCP and the token semaphore at the same time. Create
    // a child thread that waits on the semaphore and posts an I/O completion
    ioport_ = ioport;

    // create both semaphores in nonsignaled state
    if ((semaphore_enter_wait_ = CreateSemaphore(NULL, 0, 1, NULL))
        == NULL)
      Win32Fatal("CreateSemaphore/enter_wait");
    if ((semaphore_restart_ = CreateSemaphore(NULL, 0, 1, NULL))
        == NULL)
      Win32Fatal("CreateSemaphore/restart");

    // start child thread
    running_ = true;
    if ((child_ = CreateThread(NULL, 0, &SemaphoreThreadWrapper, this, 0, NULL))
        == NULL)
      Win32Fatal("CreateThread");

  } else {
    // all further invocations - allow child thread to loop
    ReleaseSemaphore(semaphore_restart_);
  }

  // wait for child thread to enter wait
  WaitForObject(semaphore_enter_wait_);
  // semaphore is now in nonsignaled state again for next run...

  // now SubprocessSet::DoWork() can enter GetQueuedCompletionStatus()...
}

bool GNUmakeTokenPoolWin32::TokenIsAvailable(ULONG_PTR key) {
  // alert child thread to break wait on token semaphore
  QueueUserAPC((PAPCFUNC)&NoopAPCFunc, child_, (ULONG_PTR)NULL);

  // return true when GetQueuedCompletionStatus() returned our key
  return key == (ULONG_PTR) this;
}

void GNUmakeTokenPoolWin32::ReleaseSemaphore(HANDLE semaphore) {
  if (!::ReleaseSemaphore(semaphore, 1, NULL))
    Win32Fatal("ReleaseSemaphore");
}

void GNUmakeTokenPoolWin32::WaitForObject(HANDLE object) {
  if (WaitForSingleObject(object, INFINITE) != WAIT_OBJECT_0)
    Win32Fatal("WaitForSingleObject");
}

TokenPool* TokenPool::Get() {
  return new GNUmakeTokenPoolWin32;
}
