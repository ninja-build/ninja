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

#include "subprocess.h"

#include <assert.h>
#include <stdio.h>

#include <algorithm>

#include "util.h"

Subprocess::Subprocess(bool use_console) : child_(NULL),
                                           use_console_(use_console) {
  memset(&overlapped_[PIPE_StdOut], 0, sizeof(overlapped_[PIPE_StdOut]));
  memset(&overlapped_[PIPE_StdErr], 0, sizeof(overlapped_[PIPE_StdErr]));
  is_reading_[PIPE_StdOut] = is_reading_[PIPE_StdErr] = false, false;
}

Subprocess::~Subprocess() {
  if (pipe_[PIPE_StdOut]) {
    if (!CloseHandle(pipe_[PIPE_StdOut]))
      Win32Fatal("CloseHandle");
  }
  if (pipe_[PIPE_StdErr]) {
    if (!CloseHandle(pipe_[PIPE_StdErr]))
      Win32Fatal("CloseHandle");
  }
  // Reap child if forgotten.
  if (child_)
    Finish();
}

HANDLE Subprocess::SetupPipe(HANDLE ioport, PipeType pipe_type) {
  char pipe_name[100];

  if (pipe_type == PIPE_StdOut)
    snprintf(pipe_name, sizeof(pipe_name),
            "\\\\.\\pipe\\ninja_pid%lu_sp%p", GetCurrentProcessId(), this);
  else
    snprintf(pipe_name, sizeof(pipe_name),
             "\\\\.\\pipe\\ninja_pid%lu_sp%p_err", GetCurrentProcessId(), this);

  pipe_[pipe_type] = ::CreateNamedPipeA(pipe_name,
                                        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                                        PIPE_TYPE_BYTE,
                                        PIPE_UNLIMITED_INSTANCES,
                                        0, 0, INFINITE, NULL);
  if (pipe_[pipe_type] == INVALID_HANDLE_VALUE)
    Win32Fatal("CreateNamedPipe");

  // multiple pipes can be associated to a completion port, but
  // the key must be different
  ULONG_PTR  key = (ULONG_PTR)this;
  if (pipe_type == PIPE_StdErr)
    key |= 1;  // since on Window 'this' will be 8-byte aligned, use
               // the last bit to encode the pipe type (0 -> PIPE_StdOut,
               //                                       1 -> PIPE_StdErr)

  if (!CreateIoCompletionPort(pipe_[pipe_type], ioport, (ULONG_PTR)key, 0))
    Win32Fatal("CreateIoCompletionPort");

  memset(&overlapped_[pipe_type], 0, sizeof(overlapped_[pipe_type]));
  if (!ConnectNamedPipe(pipe_[pipe_type], &overlapped_[pipe_type]) &&
      GetLastError() != ERROR_IO_PENDING) {
    Win32Fatal("ConnectNamedPipe");
  }

  // Get the write end of the pipe as a handle inheritable across processes.
  HANDLE output_write_handle = CreateFile(pipe_name, GENERIC_WRITE, 0,
                                          NULL, OPEN_EXISTING, 0, NULL);
  HANDLE output_write_child;
  if (!DuplicateHandle(GetCurrentProcess(), output_write_handle,
                       GetCurrentProcess(), &output_write_child,
                       0, TRUE, DUPLICATE_SAME_ACCESS)) {
    Win32Fatal("DuplicateHandle");
  }
  CloseHandle(output_write_handle);

  return output_write_child;
}

bool Subprocess::Start(SubprocessSet* set, const string& command) {
  HANDLE child_out_pipe = SetupPipe(set->ioport_, PIPE_StdOut);
  HANDLE child_err_pipe = SetupPipe(set->ioport_, PIPE_StdErr);

  SECURITY_ATTRIBUTES security_attributes;
  memset(&security_attributes, 0, sizeof(SECURITY_ATTRIBUTES));
  security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
  security_attributes.bInheritHandle = TRUE;
  // Must be inheritable so subprocesses can dup to children.
  HANDLE nul = CreateFile("NUL", GENERIC_READ,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
          &security_attributes, OPEN_EXISTING, 0, NULL);
  if (nul == INVALID_HANDLE_VALUE)
    Fatal("couldn't open nul");

  STARTUPINFOA startup_info;
  memset(&startup_info, 0, sizeof(startup_info));
  startup_info.cb = sizeof(STARTUPINFO);
  if (!use_console_) {
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = nul;
    startup_info.hStdOutput = child_out_pipe;
    startup_info.hStdError = child_err_pipe;
  }
  // In the console case, child_pipe is still inherited by the child and closed
  // when the subprocess finishes, which then notifies ninja.

  PROCESS_INFORMATION process_info;
  memset(&process_info, 0, sizeof(process_info));

  // Ninja handles ctrl-c, except for subprocesses in console pools.
  DWORD process_flags = use_console_ ? 0 : CREATE_NEW_PROCESS_GROUP;

  // Do not prepend 'cmd /c' on Windows, this breaks command
  // lines greater than 8,191 chars.
  if (!CreateProcessA(NULL, (char*)command.c_str(), NULL, NULL,
                      /* inherit handles */ TRUE, process_flags,
                      NULL, NULL,
                      &startup_info, &process_info)) {
    DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND) {
      // File (program) not found error is treated as a normal build
      // action failure.
      if (child_out_pipe)
        CloseHandle(child_out_pipe);
      if (child_err_pipe)
        CloseHandle(child_err_pipe);
      CloseHandle(pipe_[PIPE_StdOut]);
      CloseHandle(pipe_[PIPE_StdErr]);
      CloseHandle(nul);
      printf("pipe to null\n");
      pipe_[PIPE_StdOut] = NULL;
      pipe_[PIPE_StdErr] = NULL;
      // child_ is already NULL;
      buf_[PIPE_StdOut] = "CreateProcess failed: The system cannot find the file "
          "specified.\n";
      return true;
    } else {
      Win32Fatal("CreateProcess");    // pass all other errors to Win32Fatal
    }
  }

  // Close pipe channel only used by the child.
  if (child_out_pipe)
    CloseHandle(child_out_pipe);
  if (child_err_pipe)
    CloseHandle(child_err_pipe);
  CloseHandle(nul);

  CloseHandle(process_info.hThread);
  child_ = process_info.hProcess;

  return true;
}

void Subprocess::OnPipeReady(PipeType pipe_type) {
  DWORD bytes;
  if (!GetOverlappedResult(pipe_[pipe_type], &overlapped_[pipe_type],
                           &bytes, TRUE)) {
    if (GetLastError() == ERROR_BROKEN_PIPE) {
      CloseHandle(pipe_[pipe_type]);
      pipe_[pipe_type] = NULL;
      return;
    }
    Win32Fatal("GetOverlappedResult");
  }

  if (is_reading_[pipe_type] && bytes)
    buf_[pipe_type].append(overlapped_buf_[pipe_type], bytes);

  memset(&overlapped_[pipe_type], 0, sizeof(overlapped_[pipe_type]));
  is_reading_[pipe_type] = true;
  if (!::ReadFile(pipe_[pipe_type], overlapped_buf_[pipe_type],
                  sizeof(overlapped_buf_[pipe_type]),
                  &bytes, &overlapped_[pipe_type])) {
    if (GetLastError() == ERROR_BROKEN_PIPE) {
      CloseHandle(pipe_[pipe_type]);
      pipe_[pipe_type] = NULL;
      return;
    }
    if (GetLastError() != ERROR_IO_PENDING)
      Win32Fatal("ReadFile");
  }

  // Even if we read any bytes in the readfile call, we'll enter this
  // function again later and get them at that point.
}

ExitStatus Subprocess::Finish() {
  if (!child_)
    return ExitFailure;

  // TODO: add error handling for all of these.
  WaitForSingleObject(child_, INFINITE);

  DWORD exit_code = 0;
  GetExitCodeProcess(child_, &exit_code);

  CloseHandle(child_);
  child_ = NULL;

  return exit_code == 0              ? ExitSuccess :
         exit_code == CONTROL_C_EXIT ? ExitInterrupted :
                                       ExitFailure;
}

bool Subprocess::Done() const {
  return pipe_[PIPE_StdOut] == NULL && pipe_[PIPE_StdErr] == NULL;
}

string Subprocess::GetOutput() const {
  return buf_[PIPE_StdOut] + buf_[PIPE_StdErr];
}

HANDLE SubprocessSet::ioport_;

SubprocessSet::SubprocessSet() {
  ioport_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
  if (!ioport_)
    Win32Fatal("CreateIoCompletionPort");
  if (!SetConsoleCtrlHandler(NotifyInterrupted, TRUE))
    Win32Fatal("SetConsoleCtrlHandler");
}

SubprocessSet::~SubprocessSet() {
  Clear();

  SetConsoleCtrlHandler(NotifyInterrupted, FALSE);
  CloseHandle(ioport_);
}

BOOL WINAPI SubprocessSet::NotifyInterrupted(DWORD dwCtrlType) {
  if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
    if (!PostQueuedCompletionStatus(ioport_, 0, 0, NULL))
      Win32Fatal("PostQueuedCompletionStatus");
    return TRUE;
  }

  return FALSE;
}

Subprocess *SubprocessSet::Add(const string& command, bool use_console) {
  Subprocess *subprocess = new Subprocess(use_console);
  if (!subprocess->Start(this, command)) {
    delete subprocess;
    return 0;
  }
  if (subprocess->child_)
    running_.push_back(subprocess);
  else
    finished_.push(subprocess);
  return subprocess;
}

bool SubprocessSet::DoWork() {
  DWORD bytes_read;
  Subprocess* subproc;
  OVERLAPPED* overlapped;
  ULONG_PTR  ptr;

  if (!GetQueuedCompletionStatus(ioport_, &bytes_read, &ptr,
                                 &overlapped, INFINITE)) {
    if (GetLastError() != ERROR_BROKEN_PIPE)
      Win32Fatal("GetQueuedCompletionStatus");
  }

  bool is_std_err_pipe = ptr & 1;
  if (is_std_err_pipe)
    ptr &= ~1;
  subproc = (Subprocess*)ptr;

  if (!subproc) // A NULL subproc indicates that we were interrupted and is
                // delivered by NotifyInterrupted above.
    return true;

  if (!is_std_err_pipe)
    subproc->OnPipeReady(PIPE_StdOut);
  else
    subproc->OnPipeReady(PIPE_StdErr);

  if (subproc->Done()) {
    vector<Subprocess*>::iterator end =
        remove(running_.begin(), running_.end(), subproc);
    if (running_.end() != end) {
      finished_.push(subproc);
      running_.resize(end - running_.begin());
    }
  }

  return false;
}

Subprocess* SubprocessSet::NextFinished() {
  if (finished_.empty())
    return NULL;
  Subprocess* subproc = finished_.front();
  finished_.pop();
  return subproc;
}

void SubprocessSet::Clear() {
  for (vector<Subprocess*>::iterator i = running_.begin();
       i != running_.end(); ++i) {
    // Since the foreground process is in our process group, it will receive a
    // CTRL_C_EVENT or CTRL_BREAK_EVENT at the same time as us.
    if ((*i)->child_ && !(*i)->use_console_) {
      if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT,
                                    GetProcessId((*i)->child_))) {
        Win32Fatal("GenerateConsoleCtrlEvent");
      }
    }
  }
  for (vector<Subprocess*>::iterator i = running_.begin();
       i != running_.end(); ++i)
    delete *i;
  running_.clear();
}
