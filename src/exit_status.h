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

#ifndef NINJA_EXIT_STATUS_H_
#define NINJA_EXIT_STATUS_H_

// The underlying type of the ExitStatus enum, used to represent a platform-specific
// process exit code.
#ifdef _WIN32
#define EXIT_STATUS_TYPE unsigned long
#else  // !_WIN32
#define EXIT_STATUS_TYPE int
#endif  // !_WIN32

// The platform-specific value of ExitInterrupted returned by Ninja in case of use interruption (e.g. Ctrl-C).
// On Windows, the convention used by the C runtime library is to return 3, even
// though the system error `ERROR_CONTROL_C_EXIT` is 572. Some applications also return 1
// which is ExitFailure. The value 2 is chosen to preserve the existing Ninja behavior on this
// platform. On Posix, this is simply 128 + SIGINT. Note that Ninja will map SIGHUP
// and SIGTERM interrupted to ExitInterrupted as well.
#ifdef _WIN32
#  define EXIT_INTERRUPTED_VALUE  2
#else  // !_WIN32
#  define EXIT_INTERRUPTED_VALUE  130
#endif  // !_WIN32

enum ExitStatus : EXIT_STATUS_TYPE {
  ExitSuccess=0,
  ExitFailure,
  ExitInterrupted=EXIT_INTERRUPTED_VALUE,
};

#endif  // NINJA_EXIT_STATUS_H_
