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
using namespace std;

#include <iostream>

#include "version.h"

// TODO: verify that .ninja files are unchanged
// TODO: add command line arg to exit server
// TODO: Windows support
// TODO: Verify that env vars are identical

static const int max_message_size = 1024 * 100;
static const sockaddr_un server_addr = { AF_UNIX, "./.ninja-ipc-server" };
static const sockaddr_un client_addr = { AF_UNIX, "./.ninja-ipc-client" };

static int builder_pid;
void ForwardSignalAndExit(int sig, siginfo_t *, void *) {
  kill(builder_pid, sig);
  unlink(client_addr.sun_path);
  exit(1);
}

// Returns a string containing all of the state that can affect a build, such
// as ninja version and arguments. The server checks to make sure this matches
// the client before building.
string GetStateString(int argc, char **argv) {
  string state;
  for (int i = 0; i < argc; ++i) {
    state += argv[i];
    state += '\0';
  }
  state += kNinjaVersion;
  state += '\0';
  // TODO: add env vars
#if defined(linux)
  // Append the mtime of the ninja binary file. This is convenient during
  // development because each new build will have a different mtime so you'll
  // never accidentally run a stale server by accident.
  vector<char> buffer(1000);
  if (readlink("/proc/self/exe", buffer.data(), buffer.size() - 1) != -1) {
    struct stat file;
    if (stat(buffer.data(), &file) != -1) {
      state.append((char *)&file.st_mtim, sizeof(file.st_mtim));
    } else {
      perror("GetStateString stat");
      printf("path: %s\n", buffer.data());
    }
  } else {
    perror("GetStateString readlink");
  }
#endif // linux
  return state;
}

void RequestBuildFromServerInCwd(int argc, char **argv) {
  unlink(client_addr.sun_path);
  int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (fd == -1) {
    perror("socket");
    return;
  }
  if (bind(fd, (struct sockaddr*)&client_addr, sizeof(client_addr)) == -1) {
    close(fd);
    unlink(client_addr.sun_path);
    return;
  }
  if (connect(fd, (struct sockaddr*) &server_addr, sizeof(server_addr)) == -1) {
    unlink(server_addr.sun_path);
    close(fd);
    return;
  }
  // string 
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
  fds[0] = STDIN_FILENO;
  fds[1] = STDOUT_FILENO;
  fds[2] = STDERR_FILENO;

  int written = sendmsg(fd, &msg, 0);
  if (written < 0 || ((unsigned int)written) != state.size()) {
    perror("sendmsg");
    unlink(server_addr.sun_path);
    close(fd);
    return;
  }
  printf("client wrote successfully\n");
  // Receive PID of process doing build from server.
  int bytes_read = recv(fd, &builder_pid, sizeof(builder_pid), 0);
  if (bytes_read != sizeof(builder_pid)) {
    printf("client got error in recv\n");
    unlink(server_addr.sun_path);
    close(fd);
    return;
  }
  printf("client got pid %d\n", builder_pid);
  // Forward termination signals (e.g. Control-C) to builder process while
  // waiting for build to complete.
  const vector<int> signals_to_forward = { SIGINT, SIGTERM, SIGHUP };
  vector<struct sigaction> old_handlers(signals_to_forward.size());
  struct sigaction sa = {0};
  sa.sa_sigaction = &ForwardSignalAndExit;
  for (size_t i = 0; i < signals_to_forward.size(); ++i) 
    sigaction(signals_to_forward[i], &sa, &old_handlers[i]);
  // Wait for build to complete and receive exit code.
  // TODO: use a timeout on recv and check to make sure the builder is still
  // alive, in case the server died without telling us somehow.
  int exit_code;
  bytes_read = recv(fd, &exit_code, sizeof(exit_code), 0);
  // Restore original signal handlers.
  for (size_t i = 0; i < signals_to_forward.size(); ++i) 
    sigaction(signals_to_forward[i], &old_handlers[i], nullptr);  
  if (bytes_read != sizeof(exit_code)) {
    printf("client got error in recv\n");
    unlink(server_addr.sun_path);
    close(fd);
    return;
  }
  printf("client got %d from server\n", exit_code);
  if (exit_code != EXIT_CODE_SERVER_SHUTDOWN)
    exit(exit_code);
}

void ForkBuildServerInCwd(int argc, char **argv) {
  string original_state = GetStateString(argc, argv);
  if (fork())
    return; // Parent process, run a normal build.
  // Child process, run the server.

  int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (fd == -1) {
    perror("server socket");
    exit(1);
  }
  unlink(server_addr.sun_path);
  if (bind(fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
    perror("server bind");
    unlink(server_addr.sun_path);
    exit(1);
  }

  // Disconnect from the terminal and become a persistent daemon.
  setsid();
  const vector<int> std_fds = { STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO };
  for (size_t i = 0; i < std_fds.size(); i++)
    close(std_fds[i]);

  while(true) {
    char *buffer = (char*)malloc(max_message_size);
    struct iovec io = { 0 };
    io.iov_base = buffer;
    io.iov_len = max_message_size;
    // The union guarantees correct alignment of the buffer.
    union {
      char buffer[CMSG_SPACE(sizeof(int) * 3)];
      struct cmsghdr align;
    } u;

    struct sockaddr_un source;
    struct msghdr msg = { 0 };
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = u.buffer;
    msg.msg_controllen = sizeof(u.buffer);
    msg.msg_name = &source;
    msg.msg_namelen = sizeof(source);
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * std_fds.size());
    int size = recvmsg(fd, &msg, 0);
    if (size == -1) {
      perror("server recvmsg");
      exit(1);
    }
    printf("recvmsg returned\n");
    if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS || cmsg->cmsg_len != CMSG_LEN(sizeof(int) * std_fds.size())) {
      continue;
    }
    // Connect to the console of the requesting process.
    int *received_fds = (int *) CMSG_DATA(cmsg);
    for (size_t i = 0; i < std_fds.size(); i++) {
      dup2(received_fds[i], std_fds[i]);
    }

    fprintf(stdout, "hello from server\n");
    int child_pid = 0;
    child_pid = fork();
    if (child_pid == 0) {
      // Child: Check argument compatibility and run a build.
      string received_state(buffer, size);
      std::cerr << "original_state: " << original_state << std::endl;
      std::cerr << "received_state: " << received_state << std::endl;
      if (original_state != received_state) {
        fprintf(stderr, "original_state != received_state\n");
        exit(EXIT_CODE_SERVER_SHUTDOWN);
      }
      break;
    }
    // Server: send pid of child, wait for child and report status.
    sendto(fd, &child_pid, sizeof(child_pid), 0, (sockaddr*)&source, sizeof(source));
    int wait_value;
    while(wait(&wait_value) == -1);
    int status = WEXITSTATUS(wait_value);
    fprintf(stderr, "server sending status %d\n", status);
    sendto(fd, &status, sizeof(status), 0, (sockaddr*)&source, sizeof(source));
    if (status == EXIT_CODE_SERVER_SHUTDOWN) {
      printf("server got restart from child\n");
      exit(EXIT_CODE_SERVER_SHUTDOWN);
    }

    // Disconnect from the console again.
    for (size_t i = 0; i < std_fds.size(); i++) {
      close(received_fds[i]);
      close(std_fds[i]);
    }
  }
}
