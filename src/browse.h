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

#ifndef NINJA_BROWSE_H_
#define NINJA_BROWSE_H_

namespace ninja {
struct State;

/// Run in "browse" mode, which execs a Python webserver.
/// \a ninja_command is the command used to invoke ninja.
/// \a input_file is the path to the build.ninja file.
/// \a initial_target is the target name to start the browser at, or NULL.
/// This function does not return if it runs successfully.
void RunBrowsePython(const char* ninja_command,
                     const char* input_file, const char* initial_target);
}  // namespace ninja

#endif  // NINJA_BROWSE_H_
