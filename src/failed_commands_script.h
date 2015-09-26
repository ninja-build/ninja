// Copyright 2012 Google Inc. All Rights Reserved.
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

#ifndef NINJA_FAILED_COMMANDS_SCRIPT_H_
#define NINJA_FAILED_COMMANDS_SCRIPT_H_

#include <vector>
#include <string>
using namespace std;

// Forward declarations.
struct Edge;

/// Write a shell script to @a path allowing to re-run the given
/// \a failed_edges.
/// \return whether the operation succeed or not. If not error is reported
///         in \a err if not NULL.
bool WriteFailedCommandsScript(const string& path,
                               const vector<Edge*>& failed_edges,
                               string* err);

#endif // NINJA_FAILED_COMMANDS_SCRIPT_H_
