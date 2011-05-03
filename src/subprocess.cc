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

struct Subprocess::Stream {
  Stream();
  ~Stream();
  string buf_;

  int fd_;
};

Subprocess::Stream::Stream() : fd_(-1) {}
Subprocess::Stream::~Stream() {
  if (fd_ >= 0)
    close(fd_);
}

Subprocess::Subprocess() : pid_(-1) {
  stream_ = new Stream;
}
Subprocess::~Subprocess() {
  // Reap child if forgotten.
  if (pid_ != -1)
    Finish();
  delete stream_;
}

bool Subprocess::Start(const string& command) {
  int output_pipe[2];
  if (pipe(output_pipe) < 0)
    Fatal("pipe: %s", strerror(errno));
  stream_->fd_ = output_pipe[0];

  pid_ = fork();
  if (pid_ < 0)
    Fatal("fork: %s", strerror(errno));

  if (pid_ == 0) {
    close(output_pipe[0]);

    // Track which fd we use to report errors on.
    int error_pipe = output_pipe[1];
    do {
      // Open /dev/null over stdin.
      int devnull = open("/dev/null", O_WRONLY);
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

      execl("/bin/sh", "/bin/sh", "-c", command.c_str(), NULL);
    } while (false);

    // If we get here, something went wrong; the execl should have
    // replaced us.
    char* err = strerror(errno);
    int unused = write(error_pipe, err, strlen(err));
    unused = unused;  // If the write fails, there's nothing we can do.
    _exit(1);
  }

  close(output_pipe[1]);
  return true;
}

void Subprocess::OnFDReady() {
  char buf[4 << 10];
  ssize_t len = read(stream_->fd_, buf, sizeof(buf));
  if (len > 0) {
    stream_->buf_.append(buf, len);
  } else {
    if (len < 0)
      Fatal("read: %s", strerror(errno));
    close(stream_->fd_);
    stream_->fd_ = -1;
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

bool Subprocess::Done() const {
  return stream_->fd_ == -1;
}

const string& Subprocess::GetOutput() const {
  return stream_->buf_;
}

void SubprocessSet::Add(Subprocess* subprocess) {
  running_.push_back(subprocess);
}

void SubprocessSet::DoWork() {
  vector<pollfd> fds;

  map<int, Subprocess*> fd_to_subprocess;
  for (vector<Subprocess*>::iterator i = running_.begin();
       i != running_.end(); ++i) {
    int fd = (*i)->stream_->fd_;
    if (fd >= 0) {
      fd_to_subprocess[fd] = *i;
      fds.resize(fds.size() + 1);
      pollfd* newfd = &fds.back();
      newfd->fd = fd;
      newfd->events = POLLIN;
      newfd->revents = 0;
    }
  }

  int ret = poll(&fds.front(), fds.size(), -1);
  if (ret == -1) {
    if (errno != EINTR)
      perror("ninja: poll");
    return;
  }

  for (size_t i = 0; i < fds.size(); ++i) {
    if (fds[i].revents) {
      Subprocess* subproc = fd_to_subprocess[fds[i].fd];
      subproc->OnFDReady();
      if (subproc->Done()) {
        finished_.push(subproc);
        std::remove(running_.begin(), running_.end(), subproc);
        running_.resize(running_.size() - 1);
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
