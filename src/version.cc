// Copyright 2013 Google Inc. All Rights Reserved.
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

#include "version.h"

#include <stdlib.h>

#include "util.h"

const char* kNinjaVersion = "1.8.2";

void ParseVersion(const string& version, int* major, int* minor) {
  size_t end = version.find('.');
  *major = atoi(version.substr(0, end).c_str());
  *minor = 0;
  if (end != string::npos) {
    size_t start = end + 1;
    end = version.find('.', start);
    *minor = atoi(version.substr(start, end).c_str());
  }
}

void CheckNinjaVersion(const string& version) {
  int bin_major, bin_minor;
  ParseVersion(kNinjaVersion, &bin_major, &bin_minor);
  int file_major, file_minor;
  ParseVersion(version, &file_major, &file_minor);

  if (bin_major > file_major) {
    Warning("ninja executable version (%s) greater than build file "
            "ninja_required_version (%s); versions may be incompatible.",
            kNinjaVersion, version.c_str());
    return;
  }

  if ((bin_major == file_major && bin_minor < file_minor) ||
      bin_major < file_major) {
    Fatal("ninja version (%s) incompatible with build file "
          "ninja_required_version version (%s).",
          kNinjaVersion, version.c_str());
  }
}
