// Copyright 2026 Google Inc. All Rights Reserved.
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

#include "ansi_color.h"

#include <stdlib.h>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

bool EnvHasNoColor() {
  #ifdef _WIN32
  char buf[1024];
  DWORD len = GetEnvironmentVariableA("NO_COLOR", buf, sizeof(buf));
  return len > 0 && std::string(buf) != "0";
  #else
  char* no_color = std::getenv("NO_COLOR");
  return no_color && std::string(no_color) != "0";
  #endif
}

bool EnvHasCliColorForce() {
  #ifdef _WIN32
  char buf[1024];
  DWORD len = GetEnvironmentVariableA("CLICOLOR_FORCE", buf, sizeof(buf));
  return len > 0 && std::string(buf) != "0";
  #else
  char* clicolor_force = std::getenv("CLICOLOR_FORCE");
  return clicolor_force && std::string(clicolor_force) != "0";
  #endif
}

bool EnvHasForceColor() {
  #ifdef _WIN32
  char buf[1024];
  DWORD len = GetEnvironmentVariableA("FORCE_COLOR", buf, sizeof(buf));
  return len > 0 && std::string(buf) != "0";
  #else
  char* force_color = std::getenv("FORCE_COLOR");
  return force_color && std::string(force_color) != "0";
  #endif
}
