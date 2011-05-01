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
#include <limits.h>

//#include <poll.h>
//#include <unistd.h>
#include <stdio.h>
#include <string.h>
//#include <sys/wait.h>
#include <Shlwapi.h>

#include "util.h"

#ifdef _MSC_VER
#else
#define __min(x,y) min(x,y)
#endif

#if !defined(_TRUNCATE)
#define _TRUNCATE ((size_t)-1)
#endif

static void Win32Fatal(const char* fmt)
{
  char* msg_buf;
  DWORD dw = GetLastError(); 

  FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (char*) &msg_buf,
        0, NULL );

    // Display the error message and exit the process
    Fatal(fmt, msg_buf, dw); 
    LocalFree(msg_buf);
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
 

Subprocess::Stream::Stream() : fd_(NULL), state_(0) {}
Subprocess::Stream::~Stream() {
  assert(fd_ == NULL);
}

void Subprocess::Stream::ProcessInput(DWORD numbytesread) {
  if (state_) {
    if (numbytesread) {
      buf_.append(overlappedbuf_, numbytesread);

start_reading:
      memset(&overlapped_, 0, sizeof(overlapped_));
        
      if (::ReadFile(fd_, overlappedbuf_, sizeof(overlappedbuf_), &numbytesread, &overlapped_) || GetLastError() == ERROR_IO_PENDING)
        return;
    }

    state_ = 0;
    CloseHandle(fd_);
    fd_ = NULL;
    return;
  }

  state_ = 1;
  goto start_reading;
}

Subprocess::Subprocess(SubprocessSet* set_we_are_run_on) : pid_(NULL) , procset_(set_we_are_run_on) {

}

Subprocess::~Subprocess() {
  // Reap child if forgotten.
  if (pid_ != NULL)
    Finish();
}

bool Subprocess::Start(const string& command) {
  char pipe_name_out[32], pipe_name_err[32];
  _snprintf(pipe_name_out, _TRUNCATE, "\\\\.\\pipe\\ninja_%p_out", ::GetModuleHandle(NULL));
  _snprintf(pipe_name_err, _TRUNCATE, "\\\\.\\pipe\\ninja_%p_err", ::GetModuleHandle(NULL));
  
  assert(stdout_.state_ == 0);
  assert(stderr_.state_ == 0);

  if (INVALID_HANDLE_VALUE == (stdout_.fd_ = ::CreateNamedPipeA(pipe_name_out, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE, PIPE_UNLIMITED_INSTANCES, 4096, 4096, INFINITE, NULL)))
    Win32Fatal("CreateNamedPipe failed: %s");
  if (INVALID_HANDLE_VALUE == (stderr_.fd_ = ::CreateNamedPipeA(pipe_name_err, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE, PIPE_UNLIMITED_INSTANCES, 4096, 4096, INFINITE, NULL)))
    Win32Fatal("CreateNamedPipe failed: %s");

  // assign read channels to io completion ports
  if (!CreateIoCompletionPort(stdout_.fd_, g_ioport, (char*)this - (char*)0, 0))
    Win32Fatal("failed to bind pipe to io completion port: %s");
  if (!CreateIoCompletionPort(stderr_.fd_, g_ioport, (char*)this - (char*)0, 0))
    Win32Fatal("failed to bind pipe to io completion port: %s");

  memset(&stdout_.overlapped_, 0, sizeof(stdout_.overlapped_));
  if (!ConnectNamedPipe(stdout_.fd_, &stdout_.overlapped_) && GetLastError() != ERROR_IO_PENDING)
    Win32Fatal("ConnectNamedPipe failed: %s");
  memset(&stderr_.overlapped_, 0, sizeof(stderr_.overlapped_));
  if (!ConnectNamedPipe(stderr_.fd_, &stderr_.overlapped_) && GetLastError() != ERROR_IO_PENDING)
    Win32Fatal("ConnectNamedPipe failed: %s");


  // get the client pipes
  HANDLE output_write_handle = CreateFile(pipe_name_out, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  HANDLE output_write_child;
  if (!DuplicateHandle(GetCurrentProcess(),output_write_handle, GetCurrentProcess(),&output_write_child, 0, TRUE, DUPLICATE_SAME_ACCESS))
    Win32Fatal("DuplicateHandle: %s");
  CloseHandle(output_write_handle);

  HANDLE error_wirte_handle = CreateFile(pipe_name_err, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  HANDLE error_write_child;
  if (!DuplicateHandle(GetCurrentProcess(), error_wirte_handle, GetCurrentProcess(), &error_write_child, 0, TRUE, DUPLICATE_SAME_ACCESS))
    Win32Fatal("DuplicateHandle: %s");
  CloseHandle(error_wirte_handle);
 
  //accept connection
  while (stdout_.state_ != 1 || stderr_.state_ != 1)
    procset_->DoWork();

  PROCESS_INFORMATION pi;
  STARTUPINFOA si;

  // Set up the start up info struct.
  ZeroMemory(&si,sizeof(STARTUPINFO));
  si.cb = sizeof(STARTUPINFO);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = output_write_child;
  si.hStdInput  = NULL;
  si.hStdError  = error_write_child;
 
  char path[MAX_PATH]; BOOL ok = FALSE;

  const int kCmd32LiteralLen = 17; const char* kCmd32LiteralStr = "\\System32\\cmd.exe";
  const int kCmd32MiddleLen = 6; const char* kCmd32MiddleStr = "\" /c \"";
  GetSystemWindowsDirectoryA(path, sizeof(path) - kCmd32LiteralLen - 1);
  strcat_s(path, sizeof(path), kCmd32LiteralStr);
  size_t win_dir_len = strlen(path);
  char* mem = (char*) malloc(command.size() + win_dir_len + 3 + kCmd32MiddleLen);
  if (!mem)
    Fatal("out of memory: %s", strerror(errno));

  mem[0] = '"';
  memcpy(mem+1, path, win_dir_len);
  memcpy(mem+1+win_dir_len, kCmd32MiddleStr, kCmd32MiddleLen);
  memcpy(mem+1+win_dir_len+kCmd32MiddleLen, command.c_str(), command.size()+1);

  //extract executable name
  char *e,* s = mem+1+win_dir_len+kCmd32MiddleLen;
  while (isspace(*s))
    ++s;
  
  if (*s == '"') {
    while (isspace(*s))
      ++s;
    e = s;
    while (*e && *e != '"')
      ++e;
  } else {
    e = s;
    while (*e && !isspace(*e) && *e != '&' && *e != '|' && *e != '>' && *e != '<')
      ++e;
  }

  // replace '/' with '\' in the executable name
  for (char* i = s; i != e; ++i) {
    if (*i == '/')
      *i = '\\';
  }

#if 0 // old code to manually resolve the executable path name (optimization to avoid launching cmd.exe) - does not really work with relative paths"
  {
    e = s + __min(e-s,MAX_PATH-1);
    memcpy(path, s, e-s);
    path[e-s]='\0';

    // extract the executable and a path 
    char* dirsep = strrchr(path, '\\');
    char* look_up_dirs[] = {NULL, NULL};
    if (dirsep) {
      look_up_dirs[0] = (char*) malloc(dirsep-path);
      memcpy(look_up_dirs[0], path, dirsep-path);
      look_up_dirs[0][dirsep-path] = '\0';
      memmove(path, dirsep+1, strlen(dirsep+1)+1);
    }
 
    DWORD err = ERROR_NOT_ENOUGH_MEMORY;

    if (PathFindOnPathA(path, (PZPCSTR) look_up_dirs)) {
      ok = CreateProcessA(path, mem, NULL,NULL,TRUE, 0, NULL,NULL,&si,&pi);
    }

    free(mem);
  }

  if (!ok)
#endif


  {
    mem[1+win_dir_len+kCmd32MiddleLen+command.size()] = '"';
    mem[1+win_dir_len+kCmd32MiddleLen+command.size()+1] = '\0';
    ok = CreateProcessA(path, mem, NULL,NULL,TRUE, 0, NULL,NULL,&si,&pi);
    free(mem);
  }

  
  if (!ok) {
    char* msg_buf;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (char*) &msg_buf, 0, NULL);
    fprintf(stderr, "Failed to launch command \"%s\": %s", command.c_str(), msg_buf);
    LocalFree(msg_buf);
  }

  

  // close pipe channels we do not need
  if (error_write_child)
    CloseHandle(error_write_child);
  if (output_write_child)
    CloseHandle(output_write_child);
  
  if (ok) {
    CloseHandle(pi.hThread);

    pid_ = pi.hProcess; // Set global child process handle to cause threads to exit.

    return true;
  }

  // in order to close stdout_.fd_ and stderr_.fd_ we should get the NumBytesRead=0 because error_write_child and output_write_child have been just closed
  // and the DoWork will do the rest of the clean up in this case.
  while (stdout_.state_ != 0 || stderr_.state_ != 0)
    procset_->DoWork();

  return false;
}

void Subprocess::OnFDReady(int) {
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

void SubprocessSet::DoWork() {
  DWORD numbytesread; Subprocess* subproc; LPOVERLAPPED overlapped;

  BOOL ok = GetQueuedCompletionStatus(g_ioport, &numbytesread, (PULONG_PTR) &subproc, &overlapped, INFINITE);
    
  assert(GetLastError() == ERROR_BROKEN_PIPE || ok);
  assert(&subproc->stdout_.overlapped_ == overlapped || &subproc->stderr_.overlapped_ == overlapped );
  
  Subprocess::Stream* stream;
  if (overlapped == &subproc->stdout_.overlapped_)
    stream = &subproc->stdout_;
  else
    stream = &subproc->stderr_;

  stream->ProcessInput(ok ? numbytesread : 0);

  if (subproc->done()) {
    vector<Subprocess*>::iterator end = std::remove(running_.begin(), running_.end(), subproc);
    if (running_.end() != end) {
      finished_.push(subproc);
      running_.resize(end - running_.begin());
    }
  }
}

Subprocess* SubprocessSet::NextFinished() {
  if (finished_.empty())
    return NULL;
  Subprocess* subproc = finished_.front();
  finished_.pop();
  return subproc;
}
