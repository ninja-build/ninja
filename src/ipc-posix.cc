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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <string>
#include <vector>
using namespace std;

#include "util.h"
#include "version.h"

#define CHECK_ERRNO(x)                                                \
  ({                                                                  \
    typeof(x) _x = (x);                                               \
    if (_x == -1)                                                     \
      Fatal("%s:%d %s: %s", __FILE__, __LINE__, #x, strerror(errno)); \
    _x;                                                               \
  })

// POSIX implementation of IPC for requesting builds from a persistent build
// server. We use Unix domain sockets for their ability to transfer file
// descriptors between processes. This allows the server process to connect to
// the client process's terminal to receive input and print messages.

static const int fds_to_transfer[] = { STDIN_FILENO, STDOUT_FILENO,
                                       STDERR_FILENO };
static const int num_fds_to_transfer =
    sizeof(fds_to_transfer) / sizeof(fds_to_transfer[0]);
static int server_socket = -1;
static int server_connection = -1;

const sockaddr_un& ServerAddress() {
  static sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, "./.ninja_ipc");
  return address;
}
static const sockaddr_un server_address = ServerAddress();

/// In the client process we want to catch signals so we can forward them to
/// the builder process before exiting.
static int server_pid;
void ForwardSignalAndExit(int sig, siginfo_t*, void*) {
  kill(server_pid, sig);
  exit(1);
}

extern char** environ;
/// Returns a string containing all of the "environmental" state that can affect
/// a build, such as ninja version and environment variables. The server checks
/// to make sure this matches the client before building.
string GetEnvString() {
  string state;
  // Ninja version
  state += kNinjaVersion;
  state += '\0';
  // Environment variables
  for (char** env_var = environ; *env_var != NULL; env_var++) {
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
    state.append((char*)&file.st_mtim, sizeof(file.st_mtim));
#endif  // linux
  return state;
}

string GetRequestString(int argc, char** argv) {
  string state = GetEnvString();
  int env_size = state.size();
  state.insert(0, string((const char*)&env_size, sizeof(env_size)));
  // Arguments
  for (int i = 0; i < argc; ++i) {
    state += argv[i];
    state += '\0';
  }
  return state;
}

/// Allocates the structs required to send or receive a unix domain socket
/// message consisting of one int plus some file descriptors.
template <int num_fds>
struct FileDescriptorMessage {
  struct msghdr msg;
  struct iovec io;
  int data;
  int* fds;
  union {
    char cmsg_space[CMSG_SPACE(sizeof(int) * num_fds)];
    struct cmsghdr cmsg;
  };

  FileDescriptorMessage() : msg(), io(), data(0), fds((int*)CMSG_DATA(&cmsg)) {
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
void SendBuildRequestAndExit(const string& state) {
  // Connect to server socket.
  int client_socket = CHECK_ERRNO(socket(AF_UNIX, SOCK_STREAM, 0));
  if (connect(client_socket, (struct sockaddr*)&server_address,
              sizeof(server_address)) == -1) {
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
  struct sigaction sa = {};
  sa.sa_sigaction = &ForwardSignalAndExit;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGHUP, &sa, NULL);

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

void WaitForBuildRequest(Request* request) {
  static const string env = GetEnvString();
  if (!IsBuildServer())
    Fatal("Tried to wait for build request when we are not a build server.");
  // Disconnect from any open console.
  int devnull = open("/dev/null", O_RDWR);
  for (size_t i = 0; i < num_fds_to_transfer; i++)
    dup2(devnull, fds_to_transfer[i]);
  close(devnull);

  // Wait for a build request.
  server_connection = CHECK_ERRNO(accept(server_socket, NULL, NULL));
  FileDescriptorMessage<num_fds_to_transfer> message;
  message.fds[0] = -1;
  if (CHECK_ERRNO(recvmsg(server_connection, &message.msg, 0)) <
          (int)sizeof(int) ||
      message.cmsg.cmsg_len != CMSG_LEN(sizeof(fds_to_transfer)) ||
      message.fds[0] < 0 || message.data < 0)
    Fatal("Received invalid message.\n");

  // Connect to the console of the client process.
  for (size_t i = 0; i < num_fds_to_transfer; i++) {
    dup2(message.fds[i], fds_to_transfer[i]);
    close(message.fds[i]);
  }

  vector<char> buffer(message.data);
  for (int read = 0; read < (int)buffer.size();)
    read += CHECK_ERRNO(
        recv(server_connection, buffer.data() + read, buffer.size() - read, 0));

  // Check compatibility of build environment with that sent by the client. The
  // state is a string and is compatible only if it is identical.
  size_t env_len = *(int*)buffer.data();
  size_t env_start = sizeof(int);
  size_t env_end = env_start + env_len;
  if (env_end > buffer.size() || env != string(buffer.data() + env_start, env_len)) {
    AcceptRequest(false);
  }

  // The rest of the message contains request args.
  request->buf = string(buffer.data() + env_end, buffer.size() - env_end);
  request->args.clear();
  request->args.push_back(&request->buf[0]);
  for (char* c = &request->buf[0];
      c < &request->buf[0] + request->buf.size() - 1; ++c) {
    if (*c == '\0') {
      // We stop 1 character short of the end, so we know c+1 is valid.
      request->args.push_back(c + 1);
    }
  }
}

void AcceptRequest(bool compatible) {
  send(server_connection, &compatible, sizeof(compatible), 0);
  if (!compatible)
    exit(1);
  // Send our PID to the client so it can forward us any signals that come in.
  int pid = getpid();
  send(server_connection, &pid, sizeof(pid), 0);
}

void ForkBuildServer() {
  server_socket = CHECK_ERRNO(socket(AF_UNIX, SOCK_STREAM, 0));
  unlink(server_address.sun_path);
  CHECK_ERRNO(bind(server_socket, (struct sockaddr*)&server_address,
                   sizeof(server_address)));
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

void RequestBuildFromServer(int argc, char** argv) {
  if (IsBuildServer())
    return;
  const string state = GetRequestString(argc, argv);
  SendBuildRequestAndExit(state);
  ForkBuildServer();
  if (IsBuildServer())
    return;
  SendBuildRequestAndExit(state);
  Fatal("Build request should not fail after forking server.");
}

bool IsBuildServer() {
  return server_socket >= 0;
}
