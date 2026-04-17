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

bool EnvHasNoColor() {
  char* no_color = std::getenv("NO_COLOR");
  return no_color && std::string(no_color) != "0";
}

bool EnvHasCliColorForce() {
  char* clicolor_force = std::getenv("CLICOLOR_FORCE");
  return clicolor_force && std::string(clicolor_force) != "0";
}

bool EnvHasForceColor() {
  char* force_color = std::getenv("FORCE_COLOR");
  return force_color && std::string(force_color) != "0";
}
