// Copyright 2019 Google Inc. All Rights Reserved.
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

#ifndef NINJA_PUBLIC_UI_H_
#define NINJA_PUBLIC_UI_H_

#include "public/build_config.h"
#include "public/tools.h"

#ifdef _WIN32
#include "win32port.h"
#else
#include <stdint.h>
#endif

#ifdef _MSC_VER
#define NORETURN __declspec(noreturn)
#else
#define NORETURN __attribute__((noreturn))
#endif

namespace ninja {
namespace ui {

const char* Error();
const char* Info();
const char* Warning();

/// Find the function to execute for \a tool_name and return it via \a func.
/// Returns a Tool, or NULL if Ninja should exit.
const Tool* ChooseTool(const std::string& tool_name);

/// Execute ninja as the main ninja binary would
/// Does not return, prefering to exit() directly
/// to avoid potentially expensive cleanup when  destructuring
/// Ninja's state.
NORETURN void Execute(int argc, char** argv);

// Exit the program immediately with a nonzero status
void ExitNow();

/// Parse argv for command-line options.
/// Returns an exit code, or -1 if Ninja should continue.
int ReadFlags(int* argc, char*** argv,
              Options* options, BuildConfig* config);

/// Print usage information.
void Usage(const BuildConfig& config);

}  // namespace ui
}  // namespace ninja
#endif  // NINJA_PUBLIC_UI_H_
