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

#include <cstring>
#include <cassert>

#include "util.h"

bool Jobserver::Acquire() {
  // The first token is implicitly handed to the ninja process, so don't
  // acquire it from the jobserver
  if (token_count_ == 0 || AcquireToken()) {
    token_count_++;
    return true;
  }

  return false;
}

void Jobserver::Release() {
  assert(token_count_ >= 1);
  token_count_--;

  // Don't return first token to the jobserver, as it is implicitly handed
  // to the ninja process
  if (token_count_ > 0) {
    ReleaseToken();
  }
}

bool Jobserver::ParseJobserverAuth(const char* type) {
  // Return early if no make flags are passed in the environment
  const char* makeflags = getenv("MAKEFLAGS");
  if (makeflags == nullptr) {
    return false;
  }

  const char* jobserver_auth = "--jobserver-auth=";
  const char* str_begin = strstr(makeflags, jobserver_auth);
  if (str_begin == nullptr) {
    return false;
  }

  // Advance the string pointer to just past the = character
  str_begin += strlen(jobserver_auth);

  // Find end of argument to reject findings from following arguments
  const char* str_end = strchr(str_begin, ' ');

  // Find the length of the type value by searching for the following colon
  const char* str_colon = strchr(str_begin, ':');
  if (str_colon == nullptr || (str_end && str_colon > str_end)) {
    if (str_end != nullptr) {
      Warning("invalid --jobserver-auth value: '%.*s'", str_end - str_begin, str_begin);
    } else {
      Warning("invalid --jobserver-auth value: '%s'", str_begin);
    }

    return false;
  }

  // Ignore the argument if the length or the value of the type value doesn't
  // match the requested type (i.e. "fifo" on posix or "sem" on windows).
  if (strlen(type) != static_cast<size_t>(str_colon - str_begin) ||
      strncmp(str_begin, type, str_colon - str_begin)) {
    Warning("invalid jobserver type: got %.*s; expected %s",
        str_colon - str_begin, str_begin, type);
    return false;
  }

  // Advance the string pointer to just after the : character
  str_begin = str_colon + 1;

  // Save either the remaining value until a space, or just the rest of the
  // string.
  if (str_end == nullptr) {
    jobserver_name_ = std::string(str_begin);
  } else {
    jobserver_name_ = std::string(str_begin, str_end - str_begin);
  }

  if (jobserver_name_.empty()) {
    Warning("invalid --jobserver-auth value: ''");
    return false;
  }

  return true;
}
