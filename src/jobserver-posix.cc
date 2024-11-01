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

#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <cstring>

#include "util.h"

// Declare complete type for static constants in class.
constexpr char const Jobserver::kAuthKey[];
constexpr char const Jobserver::kFdsKey[];
constexpr char const Jobserver::kFifoKey[];

PosixJobserverClient::PosixJobserverClient() {
  assert(!Enabled());

  // Set name, type of pipe, and if non-parallel from MAKEFLAGS.
  Parse();

  const char* jobserver = jobserver_name_.c_str();

  // Warn if jobserver type is unknown (neither fifo nor pipe).
  if (!jobserver_fifo_ && sscanf(jobserver, "%d,%d", &rfd_, &wfd_) != 2)
    if (!jobserver_name_.empty())
      Warning("invalid jobserver auth: '%s'", jobserver);

  // Open FDs to the pipe if needed, read must be non-blocking.
  // If passed FDs are blocking on read, force non-parallel build.
  if (jobserver_fifo_) {
    rfd_ = open(jobserver + strlen(kFifoKey), O_RDONLY | O_NONBLOCK);
    wfd_ = open(jobserver + strlen(kFifoKey), O_WRONLY);
  } else if (Enabled() && (fcntl(rfd_, F_GETFL) & O_NONBLOCK) == 0) {
    jobserver_closed_ = true;
  }

  // Exit on failure to open FDs, build non-parallel for invalid passed FDs.
  if (Enabled())
    Info("using jobserver: %s", jobserver);
  else if (jobserver_fifo_ && (rfd_ == -1 || wfd_ == -1))
    Fatal("failed to open jobserver: %s: %s", jobserver, strerror(errno));
  else if (!jobserver_name_.empty())
    jobserver_closed_ = true;

  // Signal that we have initialized but do not have a token yet.
  if (Enabled())
    token_count_ = -1;
}

void PosixJobserverClient::Parse() {
  // Return early if no makeflags are passed in the environment.
  const char* makeflags = std::getenv("MAKEFLAGS");
  if (makeflags == nullptr || strlen(makeflags) == 0)
    return;

  std::string::size_type flag_char = 0;
  std::string flag;
  std::vector<std::string> flags;

  // Tokenize string to characters in flag, then words in flags.
  while (flag_char < strlen(makeflags)) {
    while (flag_char < strlen(makeflags) &&
           !isblank(static_cast<unsigned char>(makeflags[flag_char]))) {
      flag.push_back(static_cast<unsigned char>(makeflags[flag_char]));
      flag_char++;
    }

    if (!flag.empty())
      flags.push_back(flag);

    flag.clear();
    flag_char++;
  }

  // --jobserver-auth=<val>
  for (size_t n = 0; n < flags.size(); n++)
    if (flags[n].find(kAuthKey) == 0)
      flag = flags[n].substr(strlen(kAuthKey));

  // --jobserver-fds=<val>
  if (flag.empty())
    for (size_t n = 0; n < flags.size(); n++)
      if (flags[n].find(kFdsKey) == 0)
        flag = flags[n].substr(strlen(kFdsKey));

  // -j 1
  if (flag.empty())
    for (size_t n = 0; n < flags.size(); n++)
      if (flags[n].find("-j") == 0)
        jobserver_closed_ = true;

  // Check for fifo pipe.
  if (flag.find(kFifoKey) == 0)
    jobserver_fifo_ = true;

  jobserver_name_.assign(flag);
}

bool PosixJobserverClient::Enabled() const {
  return (rfd_ >= 0 && wfd_ >= 0) || jobserver_closed_;
}

unsigned char PosixJobserverClient::Acquire() {
  unsigned char token = '\0';

  // The first token is implicitly handed to a process.
  // Fallback to non-parallel if jobserver-capable parent has no pipe.
  if (token_count_ <= 0 || jobserver_closed_) {
    token_count_ = 1;
    return token;
  }

  int ret = read(rfd_, &token, 1);
  if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    jobserver_closed_ = true;
    if (!jobserver_fifo_)
      Warning("pipe closed: %d (mark the command as recursive)", rfd_);
    else
      Fatal("failed to read from jobserver: %d: %s", rfd_, strerror(errno));
  }

  if (ret > 0)
    token_count_++;

  return token;
}

void PosixJobserverClient::Release(unsigned char* token) {
  if (token_count_ < 0)
    token_count_ = 0;
  if (token_count_ > 0)
    token_count_--;

  // The first token is implicitly handed to a process.
  // Writing is not possible if the pipe is closed.
  if (*token == '\0' || jobserver_closed_)
    return;

  int ret = write(wfd_, token, 1);
  if (ret != 1) {
    Fatal("failed to write to jobserver: %d: %s", wfd_, strerror(errno));
  }

  *token = '\0';
}
