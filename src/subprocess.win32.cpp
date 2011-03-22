// Copyright 2011 Google Inc. All Rights Reserved.
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

#include <algorithm>
#include <map>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
//#include <poll.h>
//#include <unistd.h>
#include <stdio.h>
#include <string.h>
//#include <sys/wait.h>
#include <Shlwapi.h>

#include "util.h"

static void Win32Fatal(const char* fmt)
{
  char* lpMsgBuf;
  DWORD dw = GetLastError(); 

  FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (char*) &lpMsgBuf,
        0, NULL );

    // Display the error message and exit the process
    Fatal(fmt, lpMsgBuf, dw); 
    LocalFree(lpMsgBuf);
}


HANDLE g_ioport;
class IoPortCreator
{
public:
  IoPortCreator() 
  { 
    g_ioport = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, INT_MAX);
    if (!g_ioport)
      Win32Fatal("CreateIoCompletionPort failed %s:");
  }
  ~IoPortCreator()
  {
    CloseHandle(g_ioport);
  }
} g_ioportcreator;
 

Subprocess::Stream::Stream() : fd_(NULL), state(0) {}
Subprocess::Stream::~Stream() {
  assert(fd_ == NULL);
  //if (fd_ != 0)
  //{
  //  Cancel
  //  CloseHandle(fd_);
  //}
}

void Subprocess::Stream::ProcessInput(DWORD numbytesread)
{
  if (state)
  {
    if (numbytesread)
    {
      buf_.append(buf, numbytesread);

start_reading:
      memset(&overlapped, 0, sizeof(overlapped));
        
      if (::ReadFile(fd_, buf, sizeof(buf), &numbytesread, &overlapped) || GetLastError() == ERROR_IO_PENDING)
        return;
    }

    state = 0;
    CloseHandle(fd_);
    fd_ = NULL;
    return;
  }

  state = 1;
  goto start_reading;
}

Subprocess::Subprocess(SubprocessSet* pSetWeAreRunOn) : pid_(NULL) , procset_(pSetWeAreRunOn)
{

}

Subprocess::~Subprocess() {
  // Reap child if forgotten.
  if (pid_ != NULL)
    Finish();
}

bool Subprocess::Start(const string& command)
{
  char pipe_name_out[32], pipe_name_err[32];
  _snprintf_s(pipe_name_out, _TRUNCATE, "\\\\.\\pipe\\ninja_%p_out", ::GetModuleHandle(NULL));
  _snprintf_s(pipe_name_err, _TRUNCATE, "\\\\.\\pipe\\ninja_%p_err", ::GetModuleHandle(NULL));
  
  assert(stdout_.state == 0);
  assert(stderr_.state == 0);

  if (INVALID_HANDLE_VALUE == (stdout_.fd_ = ::CreateNamedPipeA(pipe_name_out, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE, PIPE_UNLIMITED_INSTANCES, 4096, 4096, INFINITE, NULL)))
    Win32Fatal("CreateNamedPipe failed: %s");
  if (INVALID_HANDLE_VALUE == (stderr_.fd_ = ::CreateNamedPipeA(pipe_name_err, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE, PIPE_UNLIMITED_INSTANCES, 4096, 4096, INFINITE, NULL)))
    Win32Fatal("CreateNamedPipe failed: %s");

  // assign read channels to io completion ports
  if (!CreateIoCompletionPort(stdout_.fd_, g_ioport, (char*)this - (char*)0, 0))
    Win32Fatal("failed to bind pipe to io completion port: %s");
  if (!CreateIoCompletionPort(stderr_.fd_, g_ioport, (char*)this - (char*)0, 0))
    Win32Fatal("failed to bind pipe to io completion port: %s");

  memset(&stdout_.overlapped, 0, sizeof(stdout_.overlapped));
  if (!ConnectNamedPipe(stdout_.fd_, &stdout_.overlapped) && GetLastError() != ERROR_IO_PENDING)
    Win32Fatal("ConnectNamedPipe failed: %s");
  memset(&stderr_.overlapped, 0, sizeof(stderr_.overlapped));
  if (!ConnectNamedPipe(stderr_.fd_, &stderr_.overlapped) && GetLastError() != ERROR_IO_PENDING)
    Win32Fatal("ConnectNamedPipe failed: %s");


  // get the client pipes
  HANDLE hOutputWrite = CreateFile(pipe_name_out, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  HANDLE hOutputWriteChild;
  if (!DuplicateHandle(GetCurrentProcess(),hOutputWrite, GetCurrentProcess(),&hOutputWriteChild, 0, TRUE, DUPLICATE_SAME_ACCESS))
    Win32Fatal("DuplicateHandle: %s");
  CloseHandle(hOutputWrite);

  HANDLE hErrWrite = CreateFile(pipe_name_err, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  HANDLE hErrWriteChild;
  if (!DuplicateHandle(GetCurrentProcess(), hErrWrite, GetCurrentProcess(), &hErrWriteChild, 0, TRUE, DUPLICATE_SAME_ACCESS))
    Win32Fatal("DuplicateHandle: %s");
  CloseHandle(hErrWrite);
 
  //accept connection
  while (stdout_.state != 1 || stderr_.state != 1)
    procset_->DoWork();

  PROCESS_INFORMATION pi;
  STARTUPINFOA si;

  // Set up the start up info struct.
  ZeroMemory(&si,sizeof(STARTUPINFO));
  si.cb = sizeof(STARTUPINFO);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = hOutputWriteChild;
  si.hStdInput  = NULL;
  si.hStdError  = hErrWriteChild;
  //si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  //si.hStdInput  = NULL;
  //si.hStdError  = NULL;
  
 
  //skip spaces
  char* mem = strdup(command.c_str());
  if (!mem)
    Fatal("out of memory: %s", strerror(errno));

  //extract executable name
  const char *e,* s = command.c_str();
  while (isspace(*s)) ++s;
  if (*s == '"')
  {
    while (isspace(*s)) ++s;
    e = s;
    while (*e && *e != '"') ++e;
  }
  else
  {
    e = s;
    while (*e && !isspace(*e)) ++e;
  }

  char path[MAX_PATH]; 
  e = s + __min(e-s,MAX_PATH-1);
  memcpy(path, s, e-s);
  path[e-s]='\0';

  // replace back slashes with forward one
  for (int i = 0; path[i]; ++i)
  {
    if (path[i] == '/')
      path[i] = '\\';
  }

  BOOL bOk = FALSE;
  if (PathFindOnPathA(path, NULL))
  {
    bOk = CreateProcessA(path, mem, NULL,NULL,TRUE, 0, NULL,NULL,&si,&pi);
  }
  DWORD err = GetLastError();

  free(mem);

  // close pipe channels we do not need
  if (hErrWriteChild)
    CloseHandle(hErrWriteChild);
  if (hOutputWriteChild)
    CloseHandle(hOutputWriteChild);
  //close(stdout_pipe[1]);
  //close(stderr_pipe[1]);
  
  if (bOk)
  {
    CloseHandle(pi.hThread);

    pid_ = pi.hProcess; // Set global child process handle to cause threads to exit.

    return true;
  }

  CloseHandle(stdout_.fd_);
  CloseHandle(stderr_.fd_);
 
  char* lpMsgBuf;
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (char*) &lpMsgBuf, 0, NULL);
  fprintf(stderr, "Failed to launch command \"%s\": %s", command.c_str(), lpMsgBuf);
  LocalFree(lpMsgBuf);

  stderr_.fd_ = NULL;//-1;
  stdout_.fd_ = NULL;
  stdout_.state = 0;
  stderr_.state = 0;

  return false;
}

void Subprocess::OnFDReady(int)
{
  // not used on this platform
}

bool Subprocess::Finish() {
  assert(pid_ != NULL);
  WaitForSingleObject(pid_, INFINITE);
  
  DWORD dwExitCode = 0;
  GetExitCodeProcess(pid_, &dwExitCode);

  CloseHandle(pid_);
  pid_ = NULL;

  if (dwExitCode == 0) {
      return true;
  }
  return false;
}

void SubprocessSet::Add(Subprocess* subprocess) {
  running_.push_back(subprocess);
}

void SubprocessSet::DoWork()
{
  DWORD numbytesread; Subprocess* subproc; LPOVERLAPPED overlapped;

  BOOL bOk = GetQueuedCompletionStatus(g_ioport, &numbytesread, (PULONG_PTR) &subproc, &overlapped, INFINITE);
    
  assert(GetLastError() == ERROR_BROKEN_PIPE || bOk);
  assert(&subproc->stdout_.overlapped == overlapped || &subproc->stderr_.overlapped == overlapped );
  
  Subprocess::Stream* stream;
  if (overlapped == &subproc->stdout_.overlapped)
    stream = &subproc->stdout_;
  else
    stream = &subproc->stderr_;

  stream->ProcessInput(bOk ? numbytesread : 0);

  if (subproc->done())
  {
    finished_.push(subproc);
    std::remove(running_.begin(), running_.end(), subproc);
    running_.resize(running_.size() - 1);
  }
}

Subprocess* SubprocessSet::NextFinished() {
  if (finished_.empty())
    return NULL;
  Subprocess* subproc = finished_.front();
  finished_.pop();
  return subproc;
}
