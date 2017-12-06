// Copyright 2016-2017 Google Inc. All Rights Reserved.
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

#include "tokenpool.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "line_printer.h"

// TokenPool implementation for GNU make jobserver
// (http://make.mad-scientist.net/papers/jobserver-implementation/)
struct GNUmakeTokenPool : public TokenPool {
  GNUmakeTokenPool();
  virtual ~GNUmakeTokenPool();

  virtual bool Acquire();
  virtual void Reserve();
  virtual void Release();
  virtual void Clear();
  virtual int GetMonitorFd();

  bool Setup(bool ignore, bool verbose, double& max_load_average);

 private:
  int available_;
  int used_;

#ifdef _WIN32
  // @TODO
#else
  int rfd_;
  int wfd_;

  struct sigaction old_act_;
  bool restore_;

  static int dup_rfd_;
  static void CloseDupRfd(int signum);

  bool CheckFd(int fd);
  bool SetAlarmHandler();
#endif

  void Return();
};

// every instance owns an implicit token -> available_ == 1
GNUmakeTokenPool::GNUmakeTokenPool() : available_(1), used_(0),
                                       rfd_(-1), wfd_(-1), restore_(false) {
}

GNUmakeTokenPool::~GNUmakeTokenPool() {
  Clear();
  if (restore_)
    sigaction(SIGALRM, &old_act_, NULL);
}

bool GNUmakeTokenPool::CheckFd(int fd) {
  if (fd < 0)
    return false;
  int ret = fcntl(fd, F_GETFD);
  if (ret < 0)
    return false;
  return true;
}

int GNUmakeTokenPool::dup_rfd_ = -1;

void GNUmakeTokenPool::CloseDupRfd(int signum) {
  close(dup_rfd_);
  dup_rfd_ = -1;
}

bool GNUmakeTokenPool::SetAlarmHandler() {
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = CloseDupRfd;
  if (sigaction(SIGALRM, &act, &old_act_) < 0) {
    perror("sigaction:");
    return(false);
  } else {
    restore_ = true;
    return(true);
  }
}

bool GNUmakeTokenPool::Setup(bool ignore,
                             bool verbose,
                             double& max_load_average) {
  const char *value = getenv("MAKEFLAGS");
  if (value) {
    // GNU make <= 4.1
    const char *jobserver = strstr(value, "--jobserver-fds=");
    // GNU make => 4.2
    if (!jobserver)
      jobserver = strstr(value, "--jobserver-auth=");
    if (jobserver) {
      LinePrinter printer;

      if (ignore) {
        printer.PrintOnNewLine("ninja: warning: -jN forced on command line; ignoring GNU make jobserver.\n");
      } else {
        int rfd = -1;
        int wfd = -1;
        if ((sscanf(jobserver, "%*[^=]=%d,%d", &rfd, &wfd) == 2) &&
            CheckFd(rfd) &&
            CheckFd(wfd) &&
            SetAlarmHandler()) {
          const char *l_arg = strstr(value, " -l");
          int load_limit = -1;

          if (verbose) {
            printer.PrintOnNewLine("ninja: using GNU make jobserver.\n");
          }
          rfd_ = rfd;
          wfd_ = wfd;

          // translate GNU make -lN to ninja -lN
          if (l_arg &&
              (sscanf(l_arg + 3, "%d ", &load_limit) == 1) &&
              (load_limit > 0)) {
            max_load_average = load_limit;
          }

          return true;
        }
      }
    }
  }

  return false;
}

bool GNUmakeTokenPool::Acquire() {
  if (available_ > 0)
    return true;

#ifdef USE_PPOLL
  pollfd pollfds[] = {{rfd_, POLLIN, 0}};
  int ret = poll(pollfds, 1, 0);
#else
  fd_set set;
  struct timeval timeout = { 0, 0 };
  FD_ZERO(&set);
  FD_SET(rfd_, &set);
  int ret = select(rfd_ + 1, &set, NULL, NULL, &timeout);
#endif
  if (ret > 0) {
    dup_rfd_ = dup(rfd_);

    if (dup_rfd_ != -1) {
      struct sigaction act, old_act;
      int ret = 0;

      memset(&act, 0, sizeof(act));
      act.sa_handler = CloseDupRfd;
      if (sigaction(SIGCHLD, &act, &old_act) == 0) {
        char buf;

        // block until token read, child exits or timeout
        alarm(1);
        ret = read(dup_rfd_, &buf, 1);
        alarm(0);

        sigaction(SIGCHLD, &old_act, NULL);
      }

      CloseDupRfd(0);

      if (ret > 0) {
        available_++;
        return true;
      }
    }
  }
  return false;
}

void GNUmakeTokenPool::Reserve() {
  available_--;
  used_++;
}

void GNUmakeTokenPool::Return() {
  const char buf = '+';
  while (1) {
    int ret = write(wfd_, &buf, 1);
    if (ret > 0)
      available_--;
    if ((ret != -1) || (errno != EINTR))
      return;
    // write got interrupted - retry
  }
}

void GNUmakeTokenPool::Release() {
  available_++;
  used_--;
  if (available_ > 1)
    Return();
}

void GNUmakeTokenPool::Clear() {
  while (used_ > 0)
    Release();
  while (available_ > 1)
    Return();
}

int GNUmakeTokenPool::GetMonitorFd() {
  return(rfd_);
}

struct TokenPool *TokenPool::Get(bool ignore,
                                 bool verbose,
                                 double& max_load_average) {
  GNUmakeTokenPool *tokenpool = new GNUmakeTokenPool;
  if (tokenpool->Setup(ignore, verbose, max_load_average))
    return tokenpool;
  else
    delete tokenpool;
  return NULL;
}
