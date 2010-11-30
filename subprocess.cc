#include "subprocess.h"

#include <algorithm>
#include <map>
#include <assert.h>
#include <errno.h>
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
}

bool Subprocess::Start(const string& command) {
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
    if (close(0) < 0 ||
        dup2(stdout_pipe[1], 1) < 0 ||
        dup2(stderr_pipe[1], 2) < 0 ||
        close(stdout_pipe[0]) < 0 ||
        close(stdout_pipe[1]) < 0 ||
        close(stderr_pipe[0]) < 0 ||
        // Leave stderr_pipe[1] alone so we can write to it on error.
        execl("/bin/sh", "/bin/sh", "-c", command.c_str(), NULL) < 0) {
      char* err = strerror(errno);
      write(stderr_pipe[1], err, strlen(err));
    }
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

bool Subprocess::Finish(string* err) {
  assert(pid_ != -1);

  int status;
  if (waitpid(pid_, &status, 0) < 0) {
    *err = strerror(errno);
    return false;
  }
  pid_ = -1;

  if (WIFEXITED(status)) {
    int exit = WEXITSTATUS(status);
    if (exit == 0)
      return true;
    *err = "nonzero exit status";
  } else {
    *err = "XXX something else went wrong";
  }
  return false;
}

void SubprocessSet::Add(Subprocess* subprocess) {
  running_.push_back(subprocess);
}

void SubprocessSet::DoWork(string* err) {
  struct pollfd* fds = new pollfd[running_.size() * 2];

  map<int, Subprocess*> fd_to_subprocess;
  int fds_count = 0;
  for (vector<Subprocess*>::iterator i = running_.begin();
       i != running_.end(); ++i) {
    int fd = (*i)->stdout_.fd_;
    if (fd >= 0) {
      fd_to_subprocess[fd] = *i;
      fds[fds_count].fd = fd;
      fds[fds_count].events = POLLIN;
      fds[fds_count].revents = 0;
      ++fds_count;
    }
    fd = (*i)->stderr_.fd_;
    if (fd >= 0) {
      fd_to_subprocess[fd] = *i;
      fds[fds_count].fd = fd;
      fds[fds_count].events = POLLIN;
      fds[fds_count].revents = 0;
      ++fds_count;
    }
  }

  int ret = poll(fds, fds_count, -1);
  if (ret == -1) {
    if (errno != EINTR)
      perror("poll");
    return;
  }

  for (int i = 0; i < fds_count; ++i) {
    if (fds[i].revents) {
      Subprocess* subproc = fd_to_subprocess[fds[i].fd];
      if (fds[i].revents) {
        subproc->OnFDReady(fds[i].fd);
        if (subproc->done()) {
          subproc->Finish(err);
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
