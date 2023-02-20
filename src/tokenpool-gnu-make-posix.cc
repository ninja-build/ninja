// Copyright 2016-2018 Google Inc. All Rights Reserved.
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

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stack>

#include "util.h"

// TokenPool implementation for GNU make jobserver - POSIX implementation
// (http://make.mad-scientist.net/papers/jobserver-implementation/)
struct GNUmakeTokenPoolPosix : public GNUmakeTokenPool {
  GNUmakeTokenPoolPosix();
  virtual ~GNUmakeTokenPoolPosix();

  virtual int GetMonitorFd();

  virtual const char* GetEnv(const char* name) { return getenv(name); };
  virtual bool SetEnv(const char* name, const char* value) {
    return setenv(name, value, 1) == 0;
  };
  virtual bool ParseAuth(const char* jobserver);
  virtual bool CreatePool(int parallelism,
                          const char* style,
                          std::string* auth);
  virtual bool AcquireToken();
  virtual bool ReturnToken();

 private:
  int rfd_;
  int wfd_;
  bool closeFds_;
  std::string fifoName_;

  struct sigaction old_act_;
  bool restore_;

  // See https://www.gnu.org/software/make/manual/html_node/POSIX-Jobserver.html
  //
  //   It’s important that when you release the job slot, you write back
  //   the same character you read. Don’t assume that all tokens are the
  //   same character different characters may have different meanings to
  //   GNU make. The order is not important, since make has no idea in
  //   what order jobs will complete anyway.
  //
  std::stack<char> tokens_;

  static int dup_rfd_;
  static void CloseDupRfd(int signum);

  bool CheckFd(int fd);
  bool CheckFifo(const char* fifo);
  bool CreateFifo(int parallelism, std::string* auth);
  bool CreatePipe(int parallelism, std::string* auth);
  bool CreateTokens(int parallelism, int rfd, int wfd);
  bool SetAlarmHandler();
};

GNUmakeTokenPoolPosix::GNUmakeTokenPoolPosix() : rfd_(-1), wfd_(-1), closeFds_(false), restore_(false) {
}

GNUmakeTokenPoolPosix::~GNUmakeTokenPoolPosix() {
  Clear();
  if (closeFds_) {
    close(wfd_);
    close(rfd_);
  }
  if (fifoName_.length() > 0)
    unlink(fifoName_.c_str());
  if (restore_)
    sigaction(SIGALRM, &old_act_, NULL);
}

bool GNUmakeTokenPoolPosix::CheckFd(int fd) {
  if (fd < 0)
    return false;
  int ret = fcntl(fd, F_GETFD);
  return ret >= 0;
}

bool GNUmakeTokenPoolPosix::CheckFifo(const char* fifo) {
  // remove possible junk from end of fifo name
  char *filename = strdup(fifo);
  char *end;
  if ((end = strchr(filename, ' ')) != NULL) {
    *end = '\0';
  }

  int rfd = open(filename, O_RDONLY);
  if (rfd < 0) {
    free(filename);
    return false;
  }

  int wfd = open(filename, O_WRONLY);
  if (wfd < 0) {
    close(rfd);
    free(filename);
    return false;
  }

  free(filename);

  rfd_ = rfd;
  wfd_ = wfd;
  closeFds_ = true;

  return true;
}

bool GNUmakeTokenPoolPosix::CreateFifo(int parallelism,
                                       std::string* auth) {
  const char* tmpdir = getenv("TMPDIR");
  char pid[32];
  snprintf(pid, sizeof(pid), "%d", getpid());

  // template copied from make/posixos.c
  std::string fifoName = tmpdir ? tmpdir : "/tmp";
  fifoName += "/GMfifo";
  fifoName += pid;

  // create jobserver named FIFO
  const char* fifoNameCStr = fifoName.c_str();
  if (mkfifo(fifoNameCStr, 0600) < 0)
    return false;

  int rfd;
  if ((rfd = open(fifoNameCStr, O_RDONLY|O_NONBLOCK)) < 0) {
    unlink(fifoNameCStr);
    return false;
  }

  int wfd;
  if ((wfd = open(fifoNameCStr, O_WRONLY)) < 0) {
    close(rfd);
    unlink(fifoNameCStr);
    return false;
  }

  if (!CreateTokens(parallelism, rfd, wfd)) {
    unlink(fifoNameCStr);
    return false;
  }

  // initialize FIFO name for this instance
  closeFds_ = true;
  fifoName_ = fifoName;

  // generate auth parameter for child processes
  *auth = "fifo:" + fifoName;

  return true;
}

bool GNUmakeTokenPoolPosix::CreatePipe(int parallelism,
                                       std::string* auth) {
  // create jobserver pipe
  int fds[2];
  if (pipe(fds) < 0)
    return false;

  if (!CreateTokens(parallelism, fds[0], fds[1]))
    return false;

  // generate auth parameter for child processes
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%d,%d", rfd_, wfd_);
  *auth = buffer;

  return true;
}

bool GNUmakeTokenPoolPosix::CreateTokens(int parallelism,
                                         int rfd,
                                         int wfd) {
  // add N tokens to pipe
  const char token = '+'; // see make/posixos.c
  while (parallelism--) {
    if (write(wfd, &token, 1) < 1) {
      close(wfd);
      close(rfd);
      return false;
    }
  }

  // initialize file descriptors for this instance
  rfd_ = rfd;
  wfd_ = wfd;

  return true;
}

int GNUmakeTokenPoolPosix::dup_rfd_ = -1;

void GNUmakeTokenPoolPosix::CloseDupRfd(int signum) {
  close(dup_rfd_);
  dup_rfd_ = -1;
}

bool GNUmakeTokenPoolPosix::SetAlarmHandler() {
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = CloseDupRfd;
  if (sigaction(SIGALRM, &act, &old_act_) < 0) {
    perror("sigaction:");
    return false;
  }
  restore_ = true;
  return true;
}

bool GNUmakeTokenPoolPosix::ParseAuth(const char* jobserver) {
  // check for jobserver-fifo style
  const char* fifo;
  if (((fifo = strstr(jobserver, "=fifo:")) != NULL) &&
      CheckFifo(fifo + 6))
    return SetAlarmHandler();

  // check for legacy simple pipe style
  int rfd = -1;
  int wfd = -1;
  if ((sscanf(jobserver, "%*[^=]=%d,%d", &rfd, &wfd) == 2) &&
      CheckFd(rfd) &&
      CheckFd(wfd) &&
      SetAlarmHandler()) {
    rfd_ = rfd;
    wfd_ = wfd;
    return true;
  }

  // some jobserver style we don't support
  return false;
}

bool GNUmakeTokenPoolPosix::CreatePool(int parallelism,
                                       const char* style,
                                       std::string* auth) {
  if (style == NULL || strcmp(style, "fifo") == 0)
    return CreateFifo(parallelism, auth);

  if (strcmp(style, "pipe") == 0)
    return CreatePipe(parallelism, auth);

  Fatal("unsupported tokenpool style '%s'", style);
}

bool GNUmakeTokenPoolPosix::AcquireToken() {
  // Please read
  //
  //   http://make.mad-scientist.net/papers/jobserver-implementation/
  //
  // for the reasoning behind the following code.
  //
  // Try to read one character from the pipe. Returns true on success.
  //
  // First check if read() would succeed without blocking.
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
    // Handle potential race condition:
    //  - the above check succeeded, i.e. read() should not block
    //  - the character disappears before we call read()
    //
    // Create a duplicate of rfd_. The duplicate file descriptor dup_rfd_
    // can safely be closed by signal handlers without affecting rfd_.
    dup_rfd_ = dup(rfd_);

    if (dup_rfd_ != -1) {
      struct sigaction act, old_act;
      int ret = 0;
      char buf;

      // Temporarily replace SIGCHLD handler with our own
      memset(&act, 0, sizeof(act));
      act.sa_handler = CloseDupRfd;
      if (sigaction(SIGCHLD, &act, &old_act) == 0) {
        struct itimerval timeout;

        // install a 100ms timeout that generates SIGALARM on expiration
        memset(&timeout, 0, sizeof(timeout));
        timeout.it_value.tv_usec = 100 * 1000; // [ms] -> [usec]
        if (setitimer(ITIMER_REAL, &timeout, NULL) == 0) {
          // Now try to read() from dup_rfd_. Return values from read():
          //
          // 1. token read                               ->  1
          // 2. pipe closed                              ->  0
          // 3. alarm expires                            -> -1 (EINTR)
          // 4. child exits                              -> -1 (EINTR)
          // 5. alarm expired before entering read()     -> -1 (EBADF)
          // 6. child exited before entering read()      -> -1 (EBADF)
          // 7. child exited before handler is installed -> go to 1 - 3
          ret = read(dup_rfd_, &buf, 1);

          // disarm timer
          memset(&timeout, 0, sizeof(timeout));
          setitimer(ITIMER_REAL, &timeout, NULL);
        }

        sigaction(SIGCHLD, &old_act, NULL);
      }

      CloseDupRfd(0);

      // Case 1 from above list
      if (ret > 0) {
        tokens_.push(buf);
        return true;
      }
    }
  }

  // read() would block, i.e. no token available,
  // cases 2-6 from above list or
  // select() / poll() / dup() / sigaction() / setitimer() failed
  return false;
}

bool GNUmakeTokenPoolPosix::ReturnToken() {
  const char buf = tokens_.top();
  while (1) {
    int ret = write(wfd_, &buf, 1);
    if (ret > 0) {
      tokens_.pop();
      return true;
    }
    if ((ret != -1) || (errno != EINTR))
      return false;
    // write got interrupted - retry
  }
}

int GNUmakeTokenPoolPosix::GetMonitorFd() {
  return rfd_;
}

TokenPool* TokenPool::Get() {
  return new GNUmakeTokenPoolPosix;
}
