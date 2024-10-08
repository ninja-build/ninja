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
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "jobserver.h"
#include "util.h"

namespace {

// Return true if |fd| is a fifo or pipe descriptor.
bool IsFifoDescriptor(int fd) {
  struct stat info;
  int ret = ::fstat(fd, &info);
  return (ret == 0) && ((info.st_mode & S_IFMT) == S_IFIFO);
}

bool SetNonBlockingFd(int fd) {
  // First, ensure FD_CLOEXEC is set.
  int flags = fcntl(fd, F_GETFL, 0);
  if (!(flags & O_NONBLOCK)) {
    int ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (ret < 0)
      return false;
  }
  return true;
}

bool SetCloseOnExecFd(int fd) {
  int flags = fcntl(fd, F_GETFD, 0);
  if (!(flags & FD_CLOEXEC)) {
    int ret = fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    if (ret < 0)
      return false;
  }
  return true;
}

// Duplicate the descriptor and make the result non-blocking and
// close-on-exec.
bool DuplicateDescriptor(int from_fd, int* to_fd) {
  int new_fd = dup(from_fd);
  if (new_fd < 0) {
    return false;
  }
  if (!SetNonBlockingFd(new_fd) || !SetCloseOnExecFd(new_fd)) {
    ::close(new_fd);
    return false;
  }
  *to_fd = new_fd;
  return true;
}

// Implementation of Jobserver::Client for Posix systems
class PosixJobserverClient : public Jobserver::Client {
 public:
  virtual ~PosixJobserverClient() {
    if (write_fd_ >= 0)
      ::close(write_fd_);
    if (read_fd_ >= 0)
      ::close(read_fd_);
  }

  Jobserver::Slot TryAcquire() override {
    if (has_implicit_slot_) {
      has_implicit_slot_ = false;
      return Jobserver::Slot::CreateImplicit();
    }
    uint8_t slot_char = '\0';
    int ret;
    do {
      ret = ::read(read_fd_, &slot_char, 1);
    } while (ret < 0 && errno == EINTR);
    if (ret == 1) {
      return Jobserver::Slot::CreateExplicit(slot_char);
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

    uint8_t slot_char = slot.GetExplicitValue();
    int ret;
    do {
      ret = ::write(write_fd_, &slot_char, 1);
    } while (ret < 0 && errno == EINTR);
    (void)ret;  // Nothing can be done in case of error here.
  }

  // Initialize instance with two explicit pipe file descriptors.
  bool InitWithPipeFds(int read_fd, int write_fd, std::string* error) {
    // Verify that the file descriptors belong to FIFOs.
    if (!IsFifoDescriptor(read_fd) || !IsFifoDescriptor(write_fd)) {
      *error = "Invalid file descriptors";
      return false;
    }
    // Duplicate the file descriptors to make then non-blocking, and
    // close-on-exec. This is important because the original descriptors
    // might be inherited by sub-processes of this client.
    if (!DuplicateDescriptor(read_fd, &read_fd_)) {
      *error = "Could not duplicate read descriptor";
      return false;
    }
    if (!DuplicateDescriptor(write_fd, &write_fd_)) {
      *error = "Could not duplicate write descriptor";
      // Let destructor close read_fd_.
      return false;
    }
    return true;
  }

  // Initialize with FIFO file path.
  bool InitWithFifo(const std::string& fifo_path, std::string* error) {
    if (fifo_path.empty()) {
      *error = "Empty fifo path";
      return false;
    }
    read_fd_ = ::open(fifo_path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (read_fd_ < 0) {
      *error =
          std::string("Error opening fifo for reading: ") + strerror(errno);
      return false;
    }
    if (!IsFifoDescriptor(read_fd_)) {
      *error = "Not a fifo path: " + fifo_path;
      // Let destructor close read_fd_.
      return false;
    }
    write_fd_ = ::open(fifo_path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (write_fd_ < 0) {
      *error =
          std::string("Error opening fifo for writing: ") + strerror(errno);
      // Let destructor close read_fd_
      return false;
    }
    return true;
  }

 private:
  // Set to true if the implicit slot has not been acquired yet.
  bool has_implicit_slot_ = true;

  // read and write descriptors.
  int read_fd_ = -1;
  int write_fd_ = -1;
};

class PosixJobserverPool : public Jobserver::Pool {
 public:
  std::string GetEnvMakeFlagsValue() const override {
    std::string result;
    if (!fifo_.empty()) {
      result.resize(fifo_.size() + 32);
      int ret = snprintf(const_cast<char*>(result.data()), result.size(),
                         " -j%zd --jobserver-auth=fifo:%s", job_count_,
                         fifo_.c_str());
      if (ret < 0 || ret > static_cast<int>(result.size()))
        Fatal("Could not format PosixJobserverPool MAKEFLAGS!");
      result.resize(static_cast<size_t>(ret));
    }
    if (read_fd_ >= 0 && write_fd_ >= 0) {
      result.resize(256);
      // See technical note in jobserver.c for formatting justification.
      int ret = snprintf(const_cast<char*>(result.data()), result.size(),
                         " -j%zu --jobserver-fds=%d,%d --jobserver-auth=%d,%d",
                         job_count_, read_fd_, write_fd_, read_fd_, write_fd_);
      if (ret < 0 || ret > static_cast<int>(result.size()))
        Fatal("Could not format PosixJobserverPool MAKEFLAGS!");
      result.resize(static_cast<size_t>(ret));
    }
    return result;
  }

  virtual ~PosixJobserverPool() {
    if (read_fd_ >= 0)
      ::close(read_fd_);
    if (write_fd_ >= 0)
      ::close(write_fd_);
    if (!fifo_.empty())
      ::unlink(fifo_.c_str());
  }

  bool InitWithPipe(size_t slot_count, std::string* error) {
    // Create anonymous pipe, then write job slot tokens into it.
    int fds[2] = { -1, -1 };
    int ret = pipe(fds);
    if (ret < 0) {
      *error =
          std::string("Could not create anonymous pipe: ") + strerror(errno);
      return false;
    }

    // The file descriptors returned by pipe() are already heritable and
    // blocking, which is exactly what's needed here.
    read_fd_ = fds[0];
    write_fd_ = fds[1];

    return FillSlots(slot_count, error);
  }

  bool InitWithFifo(size_t slot_count, std::string* error) {
    const char* tmp_dir = getenv("TMPDIR");
    if (!tmp_dir)
      tmp_dir = "/tmp";

    fifo_.resize(strlen(tmp_dir) + 32);
    int len = snprintf(const_cast<char*>(fifo_.data()), fifo_.size(),
                       "%s/NinjaFIFO%d", tmp_dir, getpid());
    if (len < 0) {
      *error = "Cannot create fifo path!";
      return false;
    }
    fifo_.resize(static_cast<size_t>(len));

    int ret = mknod(fifo_.c_str(), S_IFIFO | 0666, 0);
    if (ret < 0) {
      *error = std::string("Cannot create fifo: ") + strerror(errno);
      return false;
    }

    do {
      write_fd_ = ::open(fifo_.c_str(), O_RDWR | O_CLOEXEC);
    } while (write_fd_ < 0 && errno == EINTR);
    if (write_fd_ < 0) {
      *error = std::string("Could not open fifo: ") + strerror(errno);
      // Let destructor remove the fifo.
      return false;
    }

    return FillSlots(slot_count, error);
  }

 private:
  // Fill the pool to satisfy |slot_count| job slots. This
  // writes |slot_count - 1| bytes to the pipe to satisfy the
  // implicit job slot requirement.
  bool FillSlots(size_t slot_count, std::string* error) {
    job_count_ = slot_count;
    for (; slot_count > 1; --slot_count) {
      // Write '+' into the pipe, just like GNU Make. Note that some
      // implementations write '|' instead, but so far no client or pool
      // implementation cares about the exact value, though the official spec
      // says this might change in the future.
      const char slot_char = '+';
      int ret = ::write(write_fd_, &slot_char, 1);
      if (ret != 1) {
        if (ret < 0 && errno == EINTR)
          continue;
        *error =
            std::string("Could not fill job slots pool: ") + strerror(errno);
        return false;
      }
    }
    return true;
  }

  // Number of parallel job slots (including implicit one).
  size_t job_count_ = 0;

  // In pipe mode, these are inheritable read and write descriptors for the
  // pipe. In fifo mode, read_fd_ will be -1, and write_fd_ will be a
  // non-inheritable descriptor to keep the FIFO alive.
  int read_fd_ = -1;
  int write_fd_ = -1;

  // Path to fifo, this will be empty when using an anonymous pipe.
  std::string fifo_;
};

}  // namespace

// static
std::unique_ptr<Jobserver::Client> Jobserver::Client::Create(
    const Jobserver::Config& config, std::string* error) {
  bool success = false;
  auto client =
      std::unique_ptr<PosixJobserverClient>(new PosixJobserverClient());
  if (config.mode == Jobserver::Config::kModePipe) {
    success = client->InitWithPipeFds(config.read_fd, config.write_fd, error);
  } else if (config.mode == Jobserver::Config::kModePosixFifo) {
    success = client->InitWithFifo(config.path, error);
  } else {
    *error = "Unsupported jobserver mode";
  }
  if (!success)
    client.reset();
  return client;
}

// static
std::unique_ptr<Jobserver::Pool> Jobserver::Pool::Create(
    size_t num_job_slots, Jobserver::Config::Mode mode, std::string* error) {
  std::unique_ptr<PosixJobserverPool> pool;
  if (num_job_slots < 2) {
    *error = "At least 2 job slots needed";
    return pool;
  }
  bool success = false;
  pool.reset(new PosixJobserverPool());
  if (mode == Jobserver::Config::kModePipe) {
    success = pool->InitWithPipe(num_job_slots, error);
  } else if (mode == Jobserver::Config::kModePosixFifo) {
    success = pool->InitWithFifo(num_job_slots, error);
  } else {
    *error = "Unsupported jobserver mode";
  }
  if (!success)
    pool.reset();
  return pool;
}
