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

// ipc.h allows communication between a build server process and a build client
// process. All IPC is done relative to the current working directory of the
// process, so you can have different build servers running in different
// directories.

/// If this process is a build server, this function will return immediately.
/// Otherwise it will exit the process when the build is complete and never
/// return. If no build server is running in the current directory, one will be
/// started or forked.
void RequestBuildFromServer(int argc, char **argv);

/// Waits for a build client to connect to the server. Checks that the arguments
/// and other relevant state match between server and client before returning.
void WaitForBuildRequest(int argc, char **argv);

/// When a build server is done with a build, it must call this function to
/// inform the client before calling WaitForBuildRequest again.
void SendBuildResult(int exit_code);

/// Returns true if this process is a persistent build server, otherwise false.
/// This function may do some IPC so try to avoid calling it if -p is not
/// specified.
bool IsBuildServer();

#endif // NINJA_IPC_H