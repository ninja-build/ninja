// Copyright 2011 Google Inc. All Rights Reserved.
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

#include <algorithm>
#include <map>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

#include "util.h"

Subprocess::Stream::Stream() : fd_(-1) {}
Subprocess::Stream::~Stream() {
  if (fd_ >= 0)
    close(fd_);
}

Subprocess::Subprocess() : pid_(-1) {}
Subprocess::~Subprocess() {
  // Reap child if forgotten.
  if (pid_ != -1)
    Finish();
}

bool Subprocess::Start(const string& command) {
  command_ = command;

  int stdout_pipe[2];
  if (pipe(stdout_pipe) < 0)
    Fatal("pipe: %s", strerror(errno));
  stdout_.fd_ = stdout_pipe[0];

  int stderr_pipe[2];
  if (pipe(stderr_pipe) < 0)
    Fatal("pipe: %s", strerror(errno));
  stderr_.fd_ = stderr_pipe[0];

  pid_ = fork();
  if (pid_ < 0)
    Fatal("fork: %s", strerror(errno));

  if (pid_ == 0) {
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    // Track which fd we use to report errors on.
    int error_pipe = stderr_pipe[1];
    do {
      // Open /dev/null over stdin.
      int devnull = open("/dev/null", O_WRONLY);
      if (devnull < 0)
        break;
      if (dup2(devnull, 0) < 0)
        break;
      close(devnull);

      if (dup2(stdout_pipe[1], 1) < 0 ||
          dup2(stderr_pipe[1], 2) < 0)
        break;

      // Now can use stderr for errors.
      error_pipe = 2;
      close(stdout_pipe[1]);
      close(stderr_pipe[1]);

      execl("/bin/sh", "/bin/sh", "-c", command.c_str(), NULL);
    } while (false);

    // If we get here, something went wrong; the execl should have
    // replaced us.
    char* err = strerror(errno);
    int unused = write(error_pipe, err, strlen(err));
    unused = unused;  // If the write fails, there's nothing we can do.
    _exit(1);
  }

  close(stdout_pipe[1]);
  close(stderr_pipe[1]);
  return true;
}

void Subprocess::OnFDReady(int fd) {
  char buf[4 << 10];
  ssize_t len = read(fd, buf, sizeof(buf));
  Stream* stream = fd == stdout_.fd_ ? &stdout_ : &stderr_;
  if (len > 0) {
    stream->buf_.append(buf, len);
  } else {
    if (len < 0)
      Fatal("read: %s", strerror(errno));
    close(stream->fd_);
    stream->fd_ = -1;
  }
}

bool Subprocess::Finish() {
  assert(pid_ != -1);
  int status;
  if (waitpid(pid_, &status, 0) < 0)
    Fatal("waitpid(%d): %s", pid_, strerror(errno));
  pid_ = -1;

  if (WIFEXITED(status)) {
    int exit = WEXITSTATUS(status);
    if (exit == 0)
      return true;
  }
  return false;
}

void SubprocessSet::Add(Subprocess* subprocess) {
  running_.push_back(subprocess);
}

void SubprocessSet::DoWork() {
  vector<pollfd> fds;

  map<int, Subprocess*> fd_to_subprocess;
  for (vector<Subprocess*>::iterator i = running_.begin();
       i != running_.end(); ++i) {
    int fd = (*i)->stdout_.fd_;
    if (fd >= 0) {
      fd_to_subprocess[fd] = *i;
      fds.resize(fds.size() + 1);
      pollfd* newfd = &fds.back();
      newfd->fd = fd;
      newfd->events = POLLIN;
      newfd->revents = 0;
    }
    fd = (*i)->stderr_.fd_;
    if (fd >= 0) {
      fd_to_subprocess[fd] = *i;
      fds.resize(fds.size() + 1);
      pollfd* newfd = &fds.back();
      newfd->fd = fd;
      newfd->events = POLLIN;
      newfd->revents = 0;
    }
  }

  int ret = poll(fds.data(), fds.size(), POLL_TIMEOUT);
  if (ret == -1) {
    if (errno != EINTR)
      perror("poll");
    return;
  }
  else if (ret == 0) {
    fprintf(stderr, "\nStill waiting on\n");
    for (vector<Subprocess*>::iterator i = running_.begin();
       i != running_.end(); ++i) {
        fprintf(stderr, "\t%s\n", (*i)->get_command().c_str());
    }

    ret = poll(fds.data(), fds.size(), -1);
    if(ret == -1) {
      if (errno != EINTR)
        perror("poll");
      return;
    }
  }

  for (size_t i = 0; i < fds.size(); ++i) {
    if (fds[i].revents) {
      Subprocess* subproc = fd_to_subprocess[fds[i].fd];
      if (fds[i].revents) {
        subproc->OnFDReady(fds[i].fd);
        if (subproc->done()) {
          finished_.push(subproc);
          std::remove(running_.begin(), running_.end(), subproc);
          running_.resize(running_.size() - 1);
        }
      }
    }
  }
}

Subprocess* SubprocessSet::NextFinished() {
  if (finished_.empty())
    return NULL;
  Subprocess* subproc = finished_.front();
  finished_.pop();
  return subproc;
}
