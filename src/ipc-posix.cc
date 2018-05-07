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

#include "ipc.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <signal.h>
#include <vector>
#include <fcntl.h>
using namespace std;

#include <iostream>

#include "version.h"
#include "util.h"

// POSIX implementation of IPC for requesting builds from a persistent build
// server.

// We use Unix domain sockets for their ability to transfer file descriptors
// between processes. This allows the server process to connect to the client
// process's terminal to receive input and print messages. We use POSIX file
// locks to synchronize the client and server because they are released
// automatically when a process exits.

static const int max_message_size = 1024 * 100;
static const char *ipc_dir = "./.ninja_ipc";
/// The server lock is used by the client to check whether a server is running.
static const char *server_lock_file = "./.ninja_ipc/server_lock";
/// The build lock is used by the client to wait until the build is finished.
static const char *build_lock_file = "./.ninja_ipc/build_lock";
static const sockaddr_un server_addr = { AF_UNIX, "./.ninja_ipc/server_socket" };
static const sockaddr_un client_addr = { AF_UNIX, "./.ninja_ipc/client_socket" };
static const vector<int> std_fds = { STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO };

/// The build lock is taken in WaitForBuildRequest after a request is
/// received, and released in SendBuildResult.
static int build_lock_server_fd = -1;
/// If this process is a build server, this is set to the file descriptor of
/// the server socket.
static int server_socket = -1;
/// When the build server receives a build request, it stores the source
/// address here so the exit code can be sent back to the source later.
static struct sockaddr_un server_socket_source_address;

/// In the client process we want to catch signals so we can forward them to
/// the builder process before exiting.
static int builder_pid;
void ForwardSignalAndExit(int sig, siginfo_t *, void *) {
  kill(builder_pid, sig);
  exit(1);
}

/// Returns a string containing all of the state that can affect a build, such
/// as ninja version and arguments. The server checks to make sure this
/// matches the client before building.
string GetStateString(int argc, char **argv) {
  // Arguments
  string state;
  for (int i = 0; i < argc; ++i) {
    state += argv[i];
    state += '\0';
  }
  // Ninja version
  state += kNinjaVersion;
  state += '\0';
  // Environment variables
  for (char **env_var = environ; *env_var != NULL; env_var++) { 
    state += *env_var;
    state += '\0';
  }
#if defined(linux)
  // Append the mtime of the ninja binary file. This is convenient during
  // development because each new build will have a different mtime so you'll
  // never run a stale server by accident.
  vector<char> buffer(2000);
  struct stat file;
  if (readlink("/proc/self/exe", buffer.data(), buffer.size() - 1) != -1 &&
      stat(buffer.data(), &file) != -1)
    state.append((char *)&file.st_mtim, sizeof(file.st_mtim));
#endif // linux
  return state;
}

/// This function will only return if the server refuses to do a build because
/// of a mismatch in arguments or other state.
void RequestBuildFromServer(int argc, char **argv) {
  // Connect to server socket.
  unlink(client_addr.sun_path);
  int client_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
  bind(client_socket, (struct sockaddr*)&client_addr, sizeof(client_addr));
  connect(client_socket, (struct sockaddr*) &server_addr, sizeof(server_addr));

  // Send build request to server with current state string and the FDs of our
  // terminal so the server can write to it.
  string state = GetStateString(argc, argv);
  struct iovec io = { 0 };
  io.iov_base = (void*)state.data();
  io.iov_len = state.size();
  // The union guarantees correct alignment of the buffer.
  union {
    char buffer[CMSG_SPACE(sizeof(int) * 3)];
    struct cmsghdr align;
  } u;
  struct msghdr msg = { 0 };
  msg.msg_iov = &io;
  msg.msg_iovlen = 1;
  msg.msg_control = u.buffer;
  msg.msg_controllen = sizeof(u.buffer);
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int) * 3);
  int *fds = (int *) CMSG_DATA(cmsg);
  memcpy(fds, std_fds.data(), std_fds.size() * sizeof(int));
  sendmsg(client_socket, &msg, 0);

  // Receive PID of process doing build from server.
  recv(client_socket, &builder_pid, sizeof(builder_pid), 0);

  // Forward termination signals (e.g. Control-C) to builder process while
  // waiting for build to complete.
  const vector<int> signals_to_forward = { SIGINT, SIGTERM, SIGHUP };
  vector<struct sigaction> old_handlers(signals_to_forward.size());
  struct sigaction sa = {0};
  sa.sa_sigaction = &ForwardSignalAndExit;
  for (size_t i = 0; i < signals_to_forward.size(); ++i) 
    sigaction(signals_to_forward[i], &sa, &old_handlers[i]);

  // Wait for build to complete.
  int build_lock_client_fd = open(build_lock_file, O_RDWR);
  if (build_lock_client_fd < 0) exit(1);
  unlink(build_lock_file);
  struct flock lock = {0};
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  while (fcntl(build_lock_client_fd, F_SETLKW, &lock) < 0 && errno == EINTR);
  close(build_lock_client_fd);

  // Restore original signal handlers.
  for (size_t i = 0; i < signals_to_forward.size(); ++i)
    sigaction(signals_to_forward[i], &old_handlers[i], nullptr);

  // Read build result from socket if it is available.
  int flags = fcntl(client_socket, F_GETFL, 0);
  fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);
  int result[2] = {1 /* exit code */, 1 /* compatible */};
  recv(client_socket, &result, sizeof(result), 0);
  // If the server reports that it was compatible, exit with the given code.
  // Otherwise, return so that a new compatible server can be started.
  if (result[1])
    exit(result[0]);
}

void SendBuildResult(int exit_code, bool compatible) {
  if (build_lock_server_fd < 0)
    Fatal("SendBuildResult called when build lock not held.");
  int message[2] = {exit_code, compatible ? 1 : 0};
  sendto(server_socket, &message, sizeof(message), 0, (sockaddr*)&server_socket_source_address, sizeof(server_socket_source_address));
  close(build_lock_server_fd);
  build_lock_server_fd = -1;
}

void SendBuildResult(int exit_code) {
  SendBuildResult(exit_code, true);
}

void WaitForBuildRequest(int argc, char **argv) {
  static string server_state = GetStateString(argc, argv);

  // Disconnect from any open console.
  int devnull = open("/dev/null", O_RDWR);
  for (size_t i = 0; i < std_fds.size(); i++)
    dup2(devnull, std_fds[i]);
  close(devnull);

  // Create the build lock for clients to wait on.
  if (build_lock_server_fd != -1)
    Fatal("build lock not cleared");
  struct flock lock = {0};
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  build_lock_server_fd = open(build_lock_file, O_RDWR | O_CREAT, 0600);
  fcntl(build_lock_server_fd, F_SETLKW, &lock);

  // Setup to receive file descriptors over a unix domain socket.
  vector<char> buffer(max_message_size);
  struct iovec io = { 0 };
  io.iov_base = buffer.data();
  io.iov_len = max_message_size;
  // The union guarantees correct alignment of the buffer.
  union {
    char buffer[CMSG_SPACE(sizeof(int) * 3)];
    struct cmsghdr align;
  } u;
  memset(&server_socket_source_address, 0, sizeof(server_socket_source_address));
  struct msghdr msg = { 0 };
  msg.msg_iov = &io;
  msg.msg_iovlen = 1;
  msg.msg_control = u.buffer;
  msg.msg_controllen = sizeof(u.buffer);
  msg.msg_name = &server_socket_source_address;
  msg.msg_namelen = sizeof(server_socket_source_address);
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int) * std_fds.size());
  // Actually wait for a build request.
  int received_size = recvmsg(server_socket, &msg, 0);
  if (received_size < 0 || !cmsg || cmsg->cmsg_level != SOL_SOCKET ||
      cmsg->cmsg_type != SCM_RIGHTS ||
      cmsg->cmsg_len != CMSG_LEN(sizeof(int) * std_fds.size())) 
    Fatal("Received invalid message.\n");

  // Send our PID to the client so it can forward us any signals that come in.
  int pid = getpid();
  sendto(server_socket, &pid, sizeof(pid), 0, (sockaddr*)&server_socket_source_address, sizeof(server_socket_source_address));

  // Connect to the console of the client process.
  int *received_fds = (int *) CMSG_DATA(cmsg);
  for (size_t i = 0; i < std_fds.size(); i++) {
    dup2(received_fds[i], std_fds[i]);
    close(received_fds[i]);
  }

  // Check argument compatibility.
  string received_state(buffer.data(), received_size);
  if (server_state != received_state) {
    SendBuildResult(1, false);
    exit(1);
  }
}

void ForkBuildServer() {
  mkdir(ipc_dir, 0700);
  server_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
  unlink(server_addr.sun_path);
  bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));

  if (fork()) {
    // Parent process, continue as build client.
    close(server_socket);
    server_socket = -1;
    return;
  }

  // Hold a lock to inform clients that we are running.
  int server_lock_fd = open(server_lock_file, O_RDWR | O_CREAT, 0600);
  struct flock lock = {0};
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  fcntl(server_lock_fd, F_SETLKW, &lock);

  // Disconnect from the terminal and become a persistent daemon.
  setsid();
}

bool BuildServerIsRunning() {
  if (IsBuildServer())
    return true;
  struct flock lock = {0};
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  int fd = open(server_lock_file, O_RDWR);
  if (fd == -1)
    return false;
  fcntl(fd, F_GETLK, &lock);
  close(fd);
  return lock.l_type != F_UNLCK;
}

void MakeOrWaitForBuildRequest(int argc, char **argv) {
  if (!BuildServerIsRunning())
    ForkBuildServer();
  if (IsBuildServer())
    WaitForBuildRequest(argc, argv);
  else {
    RequestBuildFromServer(argc, argv);
    // If we get here, the server exited because of an argument mismatch. Fork
    // a new server with the correct arguments and try again.
    ForkBuildServer();
    if (IsBuildServer())
      WaitForBuildRequest(argc, argv);
    else {
      RequestBuildFromServer(argc, argv);
      Fatal("Build request should not fail after restarting server.");
    }
  }
}

bool IsBuildServer() {
  return server_socket >= 0;
}