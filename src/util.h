// Copyright 2011 Google Inc. All Rights Reserved.
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

#ifndef NINJA_UTIL_H_
#define NINJA_UTIL_H_
#pragma once

#include <stdint.h>

#include <string>
using namespace std;

#define NINJA_UNUSED_ARG(arg_name) (void)arg_name;

/// Log a fatal message and exit.
void Fatal(const char* msg, ...);

/// Log a warning message.
void Warning(const char* msg, ...);

/// Log an error message.
void Error(const char* msg, ...);

/// Canonicalize a path like "foo/../bar.h" into just "bar.h".
bool CanonicalizePath(string* path, string* err);

/// Create a directory (mode 0777 on Unix).
/// Portability abstraction.
int MakeDir(const string& path);

/// Read a file to a string.
/// Returns -errno and fills in \a err on error.
int ReadFile(const string& path, string* contents, string* err);

/// Mark a file descriptor to not be inherited on exec()s.
void SetCloseOnExec(int fd);

/// Get the current time as relative to some epoch.
/// Epoch varies between platforms; only useful for measuring elapsed
/// time.
int64_t GetTimeMillis();

/// Given a misspelled string and a NULL-terminatd list of correct spellings,
/// returns the closest match or NULL if there is no close enough match.
const char* SpellcheckString(const string& text, ...);

#ifdef _WIN32
#define snprintf _snprintf
#endif

#endif  // NINJA_UTIL_H_
