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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "line_printer.h"

// TokenPool implementation for GNU make jobserver - common bits
// every instance owns an implicit token -> available_ == 1
GNUmakeTokenPool::GNUmakeTokenPool() : available_(1), used_(0) {
}

GNUmakeTokenPool::~GNUmakeTokenPool() {
}

bool GNUmakeTokenPool::Setup(bool ignore,
                             bool verbose,
                             double& max_load_average) {
  const char* value = GetEnv("MAKEFLAGS");
  if (!value)
    return false;

  // GNU make <= 4.1
  const char* jobserver = strstr(value, "--jobserver-fds=");
  if (!jobserver)
    // GNU make => 4.2
    jobserver = strstr(value, "--jobserver-auth=");
  if (jobserver) {
    LinePrinter printer;

    if (ignore) {
      printer.PrintOnNewLine("ninja: warning: -jN forced on command line; ignoring GNU make jobserver.\n");
    } else {
      if (ParseAuth(jobserver)) {
        const char* l_arg = strstr(value, " -l");
        int load_limit = -1;

        if (verbose) {
          printer.PrintOnNewLine("ninja: using GNU make jobserver.\n");
        }

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

  return false;
}

bool GNUmakeTokenPool::Acquire() {
  if (available_ > 0)
    return true;

  if (AcquireToken()) {
    // token acquired
    available_++;
    return true;
  }

  // no token available
  return false;
}

void GNUmakeTokenPool::Reserve() {
  available_--;
  used_++;
}

void GNUmakeTokenPool::Return() {
  if (ReturnToken())
    available_--;
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
