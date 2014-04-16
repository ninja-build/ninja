// Copyright 2012 Google Inc. All Rights Reserved.
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

#include "subprocess.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

#include "util.h"

Subprocess::Subprocess(bool use_console) : fd_(-1), pid_(-1),
                                           use_console_(use_console) {
}

Subprocess::~Subprocess() {
  if (fd_ >= 0)
    close(fd_);
  // Reap child if forgotten.
  if (pid_ != -1)
    Finish();
}

bool Subprocess::Start(SubprocessSet* set, const string& command) {
  int output_pipe[2];
  if (pipe(output_pipe) < 0)
    Fatal("pipe: %s", strerror(errno));
  fd_ = output_pipe[0];
#if !defined(USE_PPOLL)
  // If available, we use ppoll in DoWork(); otherwise we use pselect
  // and so must avoid overly-large FDs.
  if (fd_ >= static_cast<int>(FD_SETSIZE))
    Fatal("pipe: %s", strerror(EMFILE));
#endif  // !USE_PPOLL
  SetCloseOnExec(fd_);

  pid_ = fork();
  if (pid_ < 0)
    Fatal("fork: %s", strerror(errno));

  if (pid_ == 0) {
    close(output_pipe[0]);

    // Track which fd we use to report errors on.
    int error_pipe = output_pipe[1];
    do {
      if (sigaction(SIGINT, &set->old_int_act_, 0) < 0)
        break;
      if (sigaction(SIGTERM, &set->old_term_act_, 0) < 0)
        break;
      if (sigprocmask(SIG_SETMASK, &set->old_mask_, 0) < 0)
        break;

      if (!use_console_) {
        // Put the child in its own session and process group. It will be
        // detached from the current terminal and ctrl-c won't reach it.
        // Since this process was just forked, it is not a process group leader
        // and setsid() will succeed.
        if (setsid() < 0)
          break;

        // Open /dev/null over stdin.
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull < 0)
          break;
        if (dup2(devnull, 0) < 0)
          break;
        close(devnull);

        if (dup2(output_pipe[1], 1) < 0 ||
            dup2(output_pipe[1], 2) < 0)
          break;

        // Now can use stderr for errors.
        error_pipe = 2;
        close(output_pipe[1]);
      }
      // In the console case, output_pipe is still inherited by the child and
      // closed when the subprocess finishes, which then notifies ninja.

      execl("/bin/sh", "/bin/sh", "-c", command.c_str(), (char *) NULL);
    } while (false);

    // If we get here, something went wrong; the execl should have
    // replaced us.
    char* err = strerror(errno);
    if (write(error_pipe, err, strlen(err)) < 0) {
      // If the write fails, there's nothing we can do.
      // But this block seems necessary to silence the warning.
    }
    _exit(1);
  }

  close(output_pipe[1]);
  return true;
}

void Subprocess::OnPipeReady() {
  char buf[4 << 10];
  ssize_t len = read(fd_, buf, sizeof(buf));
  if (len > 0) {
    buf_.append(buf, len);
  } else {
    if (len < 0)
      Fatal("read: %s", strerror(errno));
    close(fd_);
    fd_ = -1;
  }
}

ExitStatus Subprocess::Finish() {
  assert(pid_ != -1);
  int status;
  if (waitpid(pid_, &status, 0) < 0)
    Fatal("waitpid(%d): %s", pid_, strerror(errno));
  pid_ = -1;

  if (WIFEXITED(status)) {
    int exit = WEXITSTATUS(status);
    if (exit == 0)
      return ExitSuccess;
  } else if (WIFSIGNALED(status)) {
    if (WTERMSIG(status) == SIGINT || WTERMSIG(status) == SIGTERM)
      return ExitInterrupted;
  }
  return ExitFailure;
}

bool Subprocess::Done() const {
  return fd_ == -1;
}

const string& Subprocess::GetOutput() const {
  return buf_;
}

int SubprocessSet::interrupted_;

void SubprocessSet::SetInterruptedFlag(int signum) {
  interrupted_ = signum;
}

void SubprocessSet::HandlePendingInterruption() {
  sigset_t pending;
  sigemptyset(&pending);
  if (sigpending(&pending) == -1) {
    perror("ninja: sigpending");
    return;
  }
  if (sigismember(&pending, SIGINT))
    interrupted_ = SIGINT;
  else if (sigismember(&pending, SIGTERM))
    interrupted_ = SIGTERM;
}

SubprocessSet::SubprocessSet() {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);
  if (sigprocmask(SIG_BLOCK, &set, &old_mask_) < 0)
    Fatal("sigprocmask: %s", strerror(errno));

  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = SetInterruptedFlag;
  if (sigaction(SIGINT, &act, &old_int_act_) < 0)
    Fatal("sigaction: %s", strerror(errno));
  if (sigaction(SIGTERM, &act, &old_term_act_) < 0)
    Fatal("sigaction: %s", strerror(errno));
}

SubprocessSet::~SubprocessSet() {
  Clear();

  if (sigaction(SIGINT, &old_int_act_, 0) < 0)
    Fatal("sigaction: %s", strerror(errno));
  if (sigaction(SIGTERM, &old_term_act_, 0) < 0)
    Fatal("sigaction: %s", strerror(errno));
  if (sigprocmask(SIG_SETMASK, &old_mask_, 0) < 0)
    Fatal("sigprocmask: %s", strerror(errno));
}

Subprocess *SubprocessSet::Add(const string& command, bool use_console) {
  Subprocess *subprocess = new Subprocess(use_console);
  if (!subprocess->Start(this, command)) {
    delete subprocess;
    return 0;
  }
  running_.push_back(subprocess);
  return subprocess;
}

#ifdef USE_PPOLL
bool SubprocessSet::DoWork() {
  vector<pollfd> fds;
  nfds_t nfds = 0;

  for (vector<Subprocess*>::iterator i = running_.begin();
       i != running_.end(); ++i) {
    int fd = (*i)->fd_;
    if (fd < 0)
      continue;
    pollfd pfd = { fd, POLLIN | POLLPRI, 0 };
    fds.push_back(pfd);
    ++nfds;
  }

  interrupted_ = 0;
  int ret = ppoll(&fds.front(), nfds, NULL, &old_mask_);
  if (ret == -1) {
    if (errno != EINTR) {
      perror("ninja: ppoll");
      return false;
    }
    return IsInterrupted();
  }

  HandlePendingInterruption();
  if (IsInterrupted())
    return true;

  nfds_t cur_nfd = 0;
  for (vector<Subprocess*>::iterator i = running_.begin();
       i != running_.end(); ) {
    int fd = (*i)->fd_;
    if (fd < 0)
      continue;
    assert(fd == fds[cur_nfd].fd);
    if (fds[cur_nfd++].revents) {
      (*i)->OnPipeReady();
      if ((*i)->Done()) {
        finished_.push(*i);
        i = running_.erase(i);
        continue;
      }
    }
    ++i;
  }

  return IsInterrupted();
}

#else  // !defined(USE_PPOLL)
bool SubprocessSet::DoWork() {
  fd_set set;
  int nfds = 0;
  FD_ZERO(&set);

  for (vector<Subprocess*>::iterator i = running_.begin();
       i != running_.end(); ++i) {
    int fd = (*i)->fd_;
    if (fd >= 0) {
      FD_SET(fd, &set);
      if (nfds < fd+1)
        nfds = fd+1;
    }
  }

  interrupted_ = 0;
  int ret = pselect(nfds, &set, 0, 0, 0, &old_mask_);
  if (ret == -1) {
    if (errno != EINTR) {
      perror("ninja: pselect");
      return false;
    }
    return IsInterrupted();
  }

  HandlePendingInterruption();
  if (IsInterrupted())
    return true;

  for (vector<Subprocess*>::iterator i = running_.begin();
       i != running_.end(); ) {
    int fd = (*i)->fd_;
    if (fd >= 0 && FD_ISSET(fd, &set)) {
      (*i)->OnPipeReady();
      if ((*i)->Done()) {
        finished_.push(*i);
        i = running_.erase(i);
        continue;
      }
    }
    ++i;
  }

  return IsInterrupted();
}
#endif  // !defined(USE_PPOLL)

Subprocess* SubprocessSet::NextFinished() {
  if (finished_.empty())
    return NULL;
  Subprocess* subproc = finished_.front();
  finished_.pop();
  return subproc;
}

void SubprocessSet::Clear() {
  for (vector<Subprocess*>::iterator i = running_.begin();
       i != running_.end(); ++i)
    // Since the foreground process is in our process group, it will receive
    // the interruption signal (i.e. SIGINT or SIGTERM) at the same time as us.
    if (!(*i)->use_console_)
      kill(-(*i)->pid_, interrupted_);
  for (vector<Subprocess*>::iterator i = running_.begin();
       i != running_.end(); ++i)
    delete *i;
  running_.clear();
}
