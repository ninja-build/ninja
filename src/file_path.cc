// Copyright 2021 Google Inc. All Rights Reserved.
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

#include "file_path.h"
#include "util.h"

#include <string>
#include <vector>

#ifdef UNICODE

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

bool NarrowPath(const std::wstring& path, std::string* narrowPath, std::string* err) {
  std::vector<char> pathChars(PATH_MAX, '\0');
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.c_str(), -1, pathChars.data(), pathChars.size(), nullptr, nullptr)) {
    *err = std::string("Failed to narrow path: ") + GetLastErrorString();
    return false;
  }

  *narrowPath = std::string(pathChars.data());
  return true;
}

#endif
