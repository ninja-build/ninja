// Copyright 2018 Google Inc. All Rights Reserved.
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

#ifndef NINJA_IPC_H_
#define NINJA_IPC_H_

/// If this process is a build server, waits until a client requests a build
/// before returning. If this process is not a build server, this function
/// starts a build server if necessary, sends a build request to the server,
/// and then exits after the build is complete.
void MakeOrWaitForBuildRequest(int argc, char **argv);

void SendBuildResult(int exit_code);

/// Returns true if this process is a persistent build server, otherwise false.
bool IsBuildServer();

#endif // NINJA_IPC_H