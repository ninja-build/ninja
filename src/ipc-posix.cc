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
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <string>
#include <vector>
using namespace std;

#include "version.h"
#include "util.h"

#define CHECK_ERRNO(x) ({ typeof(x) _x = (x); if (_x == -1) Fatal("%s:%d %s: %s", __FILE__, __LINE__, #x, strerror(errno)); _x;})

// POSIX implementation of IPC for requesting builds from a persistent build
// server. We use Unix domain sockets for their ability to transfer file
// descriptors between processes. This allows the server process to connect to
// the client process's terminal to receive input and print messages.

static const sockaddr_un server_addr = { AF_UNIX, "./.ninja_ipc" };
static const int fds_to_transfer[] = { STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO };
static const int num_fds_to_transfer = sizeof(fds_to_transfer) / sizeof(fds_to_transfer[0]);
static int server_socket = -1;
static int server_connection = -1;

/// In the client process we want to catch signals so we can forward them to
/// the builder process before exiting.
static int server_pid;
void ForwardSignalAndExit(int sig, siginfo_t *, void *) {
  kill(server_pid, sig);
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

/// Allocates the structs required to send or receive a unix domain socket
/// message consisting of one int plus some file descriptors.
template<int num_fds> struct FileDescriptorMessage {
  struct msghdr msg = {0};
  struct iovec io = {0};
  int *fds = (int*)CMSG_DATA(&cmsg);
  int data = 0;
  union {
    char cmsg_space[CMSG_SPACE(sizeof(int) * num_fds)];
    struct cmsghdr cmsg;
  };

  FileDescriptorMessage() {
    io.iov_base = &data;
    io.iov_len = sizeof(data);
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = &cmsg;
    msg.msg_controllen = CMSG_LEN(sizeof(int) * num_fds);
    cmsg.cmsg_level = SOL_SOCKET;
    cmsg.cmsg_type = SCM_RIGHTS;
    cmsg.cmsg_len = CMSG_LEN(sizeof(int) * num_fds);
  }
};

/// This function will only return if the server refuses to do a build because
/// of a mismatch in arguments or other state.
void RequestBuildFromServer(const string &state) {
  // Connect to server socket.
  int client_socket = CHECK_ERRNO(socket(AF_UNIX, SOCK_STREAM, 0));
  if (connect(client_socket, (struct sockaddr*) &server_addr, sizeof(server_addr)) == -1) {
    // Server not running.
    close(client_socket);
    return;
  }

  // Send build request to server with current state string and the FDs of our
  // terminal so the server can write to it.
  FileDescriptorMessage<num_fds_to_transfer> message;
  message.data = state.size();
  memcpy(message.fds, fds_to_transfer, sizeof(fds_to_transfer));
  CHECK_ERRNO(sendmsg(client_socket, &message.msg, 0));
  send(client_socket, state.data(), state.size(), 0);

  // Check state compatibility.
  int compatible = 0;
  CHECK_ERRNO(recv(client_socket, &compatible, sizeof(compatible), 0));
  if (!compatible) {
    close(client_socket);
    return;
  }

  // Forward termination signals (e.g. Control-C) to server while waiting for
  // build to complete.
  CHECK_ERRNO(recv(client_socket, &server_pid, sizeof(server_pid), 0));
  struct sigaction sa = {0};
  sa.sa_sigaction = &ForwardSignalAndExit;
  for (int sig : vector<int>{ SIGINT, SIGTERM, SIGHUP })
    sigaction(sig, &sa, nullptr);

  // Read build result from socket.
  int result = 1;
  recv(client_socket, &result, sizeof(result), 0);
  close(client_socket);
  exit(result);
}

void SendBuildResult(int exit_code) {
  if (server_connection < 0)
    Fatal("SendBuildResult called when not connected.");
  send(server_connection, &exit_code, sizeof(exit_code), 0);
  close(server_connection);
  server_connection = -1;
}

void WaitForBuildRequest(const string &state) {
  // Disconnect from any open console.
  int devnull = open("/dev/null", O_RDWR);
  for (size_t i = 0; i < num_fds_to_transfer; i++)
    dup2(devnull, fds_to_transfer[i]);
  close(devnull);

  // Wait for a build request.
  server_connection = CHECK_ERRNO(accept(server_socket, NULL, NULL));
  FileDescriptorMessage<num_fds_to_transfer> message;
  message.fds[0] = -1;
  if (CHECK_ERRNO(recvmsg(server_connection, &message.msg, 0)) < (int)sizeof(int) || 
      message.cmsg.cmsg_len != CMSG_LEN(sizeof(fds_to_transfer)) ||
      message.fds[0] < 0 || message.data < 0)
    Fatal("Received invalid message.\n");

  // Connect to the console of the client process.
  for (size_t i = 0; i < num_fds_to_transfer; i++) {
    dup2(message.fds[i], fds_to_transfer[i]);
    close(message.fds[i]);
  }

  // Check compatibility of build state with that sent by the client. The
  // state is a string and is compatible only if it is identical.
  vector<char> buffer(message.data);
  for (int read = 0; read < (int)buffer.size();)
    read += CHECK_ERRNO(recv(server_connection, buffer.data() + read, buffer.size() - read, 0));
  int compatible = state == string(buffer.data(), buffer.size());
  send(server_connection, &compatible, sizeof(compatible), 0);
  if (!compatible)
    exit(1);

  // Send our PID to the client so it can forward us any signals that come in.
  int pid = getpid();
  send(server_connection, &pid, sizeof(pid), 0);
}

void ForkBuildServer() {
  server_socket = CHECK_ERRNO(socket(AF_UNIX, SOCK_STREAM, 0));
  unlink(server_addr.sun_path);
  CHECK_ERRNO(bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)));
  CHECK_ERRNO(listen(server_socket, 0));

  if (fork()) {
    // Parent process, continue as build client.
    close(server_socket);
    server_socket = -1;
  } else {
    // Disconnect from the terminal and become a persistent daemon.
    setsid();
  }
}

void MakeOrWaitForBuildRequest(int argc, char **argv) {
  static const string state = GetStateString(argc, argv);
  if (IsBuildServer()) {
    WaitForBuildRequest(state);
  } else {
    RequestBuildFromServer(state);
    // If we get here, we failed to request a build from the server. It was
    // either not running or it exited without building, possibly because the
    // state was wrong. Fork a new server with the correct state and try
    // again.
    ForkBuildServer();
    if (IsBuildServer()) {
      WaitForBuildRequest(state);
    } else {
      RequestBuildFromServer(state);
      Fatal("Build request should not fail after forking server.");
    }
  }
}

bool IsBuildServer() {
  return server_socket >= 0;
}
