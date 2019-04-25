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

#include <windows.h>
#include <stdio.h>
#include <string>
#include <algorithm>
using namespace std;

#include "util.h"
#include "version.h"

static const int max_message_size = 1024 * 256;
static const int exit_code_build_not_attempted = 2;

string GetCwd() {
  int size = GetCurrentDirectory(0, NULL);
  vector<char> cwd(size);
  GetCurrentDirectory(size, cwd.data());
  return cwd.data();
}

// Returns a string containing all of the state that can affect a build, such as
// ninja version and arguments. The server checks to make sure this matches the
// client before building.
string GetStateString(int argc, char **argv) {
  string state;
  // If the current working directory is longer than 246 characters then it will
  // be truncated in the pipe name, so we need to check equality of the full
  // path too.
  state += GetCwd();
  state += '\0';
  for (int i = 0; i < argc; ++i) {
    string arg;
    GetWin32EscapedString(argv[i], &arg);
    state += arg;
    state += '\0';
  }
  state += kNinjaVersion;
  state += '\0';
  // Append environment variables.
  LPTCH env = GetEnvironmentStrings();
  LPTCH end = env;
  // Find the end of the environment variables, marked by two consecutive null
  // chars.
  while (*end || *(end + 1))
    ++end;
  state.append(env, sizeof(TCHAR) * (end - env));
  FreeEnvironmentStrings(env);
  return state;
}

string GetPipeName() {
  const int max_len = 246; // max pipe length - required pipe prefix length
  string cwd = GetCwd().substr(0, max_len);
  replace(cwd.begin(), cwd.end(), '\\', '/');
  return R"(\\.\pipe\)" + cwd;
}

string GetEventName() {
  string cwd = GetCwd().substr(0, MAX_PATH);
  replace(cwd.begin(), cwd.end(), '\\', '/');
  return cwd;
}

void StartServer(int argc, char **argv) {
  // Assemble command line.
  string args;
  for (int i = 0; i < argc; i++) {
    string escaped;
    GetWin32EscapedString(argv[i], &escaped);
    args += escaped;
    args += ' ';
  }
  STARTUPINFO si = {0};
  PROCESS_INFORMATION pi = {0};
  HANDLE pipe_created_event = CreateEvent(NULL, TRUE, FALSE, GetEventName().c_str());
  if (!CreateProcess(NULL, (char*)args.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
    Win32Fatal("CreateProcess");
  }
  WaitForSingleObject(pipe_created_event, INFINITE);
  WaitNamedPipe(GetPipeName().c_str(), NMPWAIT_WAIT_FOREVER);
  CloseHandle(pipe_created_event);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
}

int SendRequestAndGetExitCode(int argc, char **argv) {
  string name = GetPipeName();
  // If another client is already talking to the server, wait for it.
  if (!WaitNamedPipe(name.c_str(), NMPWAIT_WAIT_FOREVER)) {
    // Pipe doesn't exist.
    StartServer(argc, argv);
  }
  vector<char> send_buffer(max_message_size);
  vector<char> receive_buffer(max_message_size);
  int pid = GetCurrentProcessId();
  memcpy(send_buffer.data(), &pid, sizeof(pid));
  string state = GetStateString(argc, argv);
  if (state.size() > max_message_size - sizeof(pid))
    Fatal("State too large.");
  memcpy(send_buffer.data() + sizeof(pid), state.data(), state.size());
  HANDLE client_pipe = CreateFile(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  if (client_pipe == INVALID_HANDLE_VALUE) Win32Fatal("CreateFile");
  DWORD bytes_written = 0;
  if (!WriteFile(client_pipe, send_buffer.data(), state.size() + sizeof(pid), &bytes_written, NULL))
    Win32Fatal("write to pipe");
  if (bytes_written != state.size() + sizeof(pid))
    Fatal("Didn't send correct number of bytes.");
  int exit_code = 1;
  DWORD bytes_read = 0;
  if (ReadFile(client_pipe, receive_buffer.data(), receive_buffer.size(), &bytes_read, nullptr)
      && bytes_read == sizeof(int)) {
    exit_code = *(int*)receive_buffer.data();
  }
  CloseHandle(client_pipe);
  return exit_code;
}

static HANDLE server_pipe = INVALID_HANDLE_VALUE;
static bool is_build_server = false;
static bool checked_for_build_server = false;

bool IsBuildServer() {
  if (checked_for_build_server) return is_build_server;
  checked_for_build_server = true;  

  if (server_pipe == INVALID_HANDLE_VALUE) {
    HANDLE pipe_created_event = OpenEvent(EVENT_MODIFY_STATE, FALSE, GetEventName().c_str());
    if (pipe_created_event == NULL) {
      is_build_server = false;
    } else {
      is_build_server = true;
      server_pipe = CreateNamedPipe(GetPipeName().c_str(),
                                    PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS | PIPE_WAIT,
                                    1, max_message_size, max_message_size, 0, NULL);
      if (server_pipe == INVALID_HANDLE_VALUE) {
        Win32Fatal("CreateNamedPipe");
      }
      SetEvent(pipe_created_event);
      CloseHandle(pipe_created_event);
    }
  }
  return is_build_server; 
}

void SendBuildResult(int exit_code) {
  if (!IsBuildServer())
    Fatal("SendBuildResult called when we are not a build server.");
  DWORD bytes_written;
  if (!WriteFile(server_pipe, &exit_code, sizeof(exit_code), &bytes_written, NULL)
      || bytes_written != sizeof(exit_code)) {
    Fatal("Write failed in SendBuildResult");
  }
}

void WaitForBuildRequest(int argc, char **argv) {
  if (!IsBuildServer())
    Fatal("WaitForBuildRequest called when we are not a build server.");
  DisconnectNamedPipe(server_pipe);
  FreeConsole();
  if (!ConnectNamedPipe(server_pipe, NULL)) {
    Win32Fatal("ConnectNamedPipe");
  }
  vector<char> receive_buffer(max_message_size);
  DWORD bytes_read;
  if (!ReadFile(server_pipe, receive_buffer.data(), receive_buffer.size(), &bytes_read, NULL)
      || bytes_read < sizeof(int)) {
    Win32Fatal("ReadFile");
  }
  int client_pid = *(int*)receive_buffer.data();
  string state = GetStateString(argc, argv);
  string client_state(receive_buffer.data() + sizeof(int), bytes_read - sizeof(int));
  if (state != client_state) {
    SendBuildResult(exit_code_build_not_attempted);
    exit(exit_code_build_not_attempted);
  }
  AttachConsole(client_pid);
}

void MakeOrWaitForBuildRequest(int argc, char **argv) {
  if (IsBuildServer()) {
    WaitForBuildRequest(argc, argv);
    return;
  }
  int exit_code = SendRequestAndGetExitCode(argc, argv);
  if (exit_code == exit_code_build_not_attempted) {
    // The server exited without attempting a build, probably because the
    // arguments changed. Try again, which will start a new server with the
    // right arguments. But first, wait for the server to delete the pipe.
    WaitNamedPipe(GetPipeName().c_str(), NMPWAIT_WAIT_FOREVER);
    exit_code = SendRequestAndGetExitCode(argc, argv);
  }
  exit(exit_code);
}
