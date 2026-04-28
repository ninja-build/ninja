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
#include "test.h"

#ifdef _WIN32
#include <windows.h>
#endif

TEST(AnsiColorTest, CliColorForce) {
  #ifdef _WIN32
  const char* temp_value = std::getenv("CLICOLOR_FORCE");
  std::string original_value = temp_value ? temp_value : "0";
  _putenv_s("CLICOLOR_FORCE", "1");
  ASSERT_EQ(true, EnvHasCliColorForce());
  _putenv_s("CLICOLOR_FORCE", "0");
  ASSERT_EQ(false, EnvHasCliColorForce());
  _putenv_s("CLICOLOR_FORCE", original_value.c_str());
  #else
  const char* temp_value = std::getenv("CLICOLOR_FORCE");
  std::string original_value = temp_value ? temp_value : "0";
  setenv("CLICOLOR_FORCE", "1", 1);
  ASSERT_EQ(true, EnvHasCliColorForce());
  setenv("CLICOLOR_FORCE", "0", 1);
  ASSERT_EQ(false, EnvHasCliColorForce());
  setenv("CLICOLOR_FORCE", original_value.c_str(), 1);
  #endif
}

TEST(AnsiColorTest, ForceColor) {
  #ifdef _WIN32
  const char* temp_value = std::getenv("FORCE_COLOR");
  std::string original_value = temp_value ? temp_value : "0";
  _putenv_s("FORCE_COLOR", "1");
  ASSERT_EQ(true, EnvHasForceColor());
  _putenv_s("FORCE_COLOR", "0");
  ASSERT_EQ(false, EnvHasForceColor());
  _putenv_s("FORCE_COLOR", original_value.c_str());
  #else
  const char* temp_value = std::getenv("FORCE_COLOR");
  std::string original_value = temp_value ? temp_value : "0";
  setenv("FORCE_COLOR", "1", 1);
  ASSERT_EQ(true, EnvHasForceColor());
  setenv("FORCE_COLOR", "0", 1);
  ASSERT_EQ(false, EnvHasForceColor());
  setenv("FORCE_COLOR", original_value.c_str(), 1);
  #endif
}

TEST(AnsiColorTest, NoColor) {
  #ifdef _WIN32
  const char* temp_value = std::getenv("NO_COLOR");
  std::string original_value = temp_value ? temp_value : "0";
  _putenv_s("NO_COLOR", "1");
  ASSERT_EQ(true, EnvHasNoColor());
  _putenv_s("NO_COLOR", "0");
  ASSERT_EQ(false, EnvHasNoColor());
  _putenv_s("NO_COLOR", original_value.c_str());
  #else
  const char* temp_value = std::getenv("NO_COLOR");
  std::string original_value = temp_value ? temp_value : "0";
  setenv("NO_COLOR", "1", 1);
  ASSERT_EQ(true, EnvHasNoColor());
  setenv("NO_COLOR", "0", 1);
  ASSERT_EQ(false, EnvHasNoColor());
  setenv("NO_COLOR", original_value.c_str(), 1);
  #endif
}
