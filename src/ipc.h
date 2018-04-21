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

#define EXIT_CODE_SERVER_SHUTDOWN 2

// If there is a persistent server running in the current working directory,
// sends an IPC request to start a build. If a build is started, waits for the
// build to finish and then calls exit() with the return code from the server.
// If a build cannot be started (perhaps there is no server, or the server's
// arguments don't match) then this function returns. In this case the current
// process should fork a new server and then perform a build.
void RequestBuildFromServerInCwd(int argc, char **argv);

// Forks a new persistent server process that will wait for future build
// requests in the current working directory. Each time a build is requested via
// IPC, the build arguments are checked. If they are exactly equal, then a new
// child process will be forked and this function will return in the child so
// that the build can proceed. Otherwise, exit() is called, because the server
// needs to be replaced.
void ForkBuildServerInCwd(int argc, char **argv);

#endif // NINJA_IPC_H