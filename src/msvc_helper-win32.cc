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

#include "msvc_helper.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "includes_normalize.h"
#include "util.h"

namespace {

/// Return true if \a input ends with \a needle.
bool EndsWith(const string& input, const string& needle) {
  return (input.size() >= needle.size() &&
          input.substr(input.size() - needle.size()) == needle);
}

string Replace(const string& input, const string& find, const string& replace) {
  string result = input;
  size_t start_pos = 0;
  while ((start_pos = result.find(find, start_pos)) != string::npos) {
    result.replace(start_pos, find.length(), replace);
    start_pos += replace.length();
  }
  return result;
}

}  // anonymous namespace

string EscapeForDepfile(const string& path) {
  // Depfiles don't escape single \.
  return Replace(path, " ", "\\ ");
}

// static
string CLParser::FilterShowIncludes(const string& line,
                                    const string& deps_prefix) {
  const string kDepsPrefixEnglish = "Note: including file: ";
  const char* in = line.c_str();
  const char* end = in + line.size();
  const string& prefix = deps_prefix.empty() ? kDepsPrefixEnglish : deps_prefix;
  if (end - in > (int)prefix.size() &&
      memcmp(in, prefix.c_str(), (int)prefix.size()) == 0) {
    in += prefix.size();
    while (*in == ' ')
      ++in;
    return line.substr(in - line.c_str());
  }
  return "";
}

// static
bool CLParser::IsSystemInclude(string path) {
  transform(path.begin(), path.end(), path.begin(), ::tolower);
  // TODO: this is a heuristic, perhaps there's a better way?
  return (path.find("program files") != string::npos ||
          path.find("microsoft visual studio") != string::npos);
}

// static
bool CLParser::FilterInputFilename(string line) {
  transform(line.begin(), line.end(), line.begin(), ::tolower);
  // TODO: other extensions, like .asm?
  return EndsWith(line, ".c") ||
      EndsWith(line, ".cc") ||
      EndsWith(line, ".cxx") ||
      EndsWith(line, ".cpp");
}

string CLParser::Parse(const string& output, const string& deps_prefix) {
  string filtered_output;

  // Loop over all lines in the output to process them.
  size_t start = 0;
  while (start < output.size()) {
    size_t end = output.find_first_of("\r\n", start);
    if (end == string::npos)
      end = output.size();
    string line = output.substr(start, end - start);

    string include = FilterShowIncludes(line, deps_prefix);
    if (!include.empty()) {
      include = IncludesNormalize::Normalize(include, NULL);
      if (!IsSystemInclude(include))
        includes_.insert(include);
    } else if (FilterInputFilename(line)) {
      // Drop it.
      // TODO: if we support compiling multiple output files in a single
      // cl.exe invocation, we should stash the filename.
    } else {
      filtered_output.append(line);
      filtered_output.append("\n");
    }

    if (end < output.size() && output[end] == '\r')
      ++end;
    if (end < output.size() && output[end] == '\n')
      ++end;
    start = end;
  }

  return filtered_output;
}

int CLWrapper::Run(const string& command, string* output) {
  SECURITY_ATTRIBUTES security_attributes = {};
  security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
  security_attributes.bInheritHandle = TRUE;

  // Must be inheritable so subprocesses can dup to children.
  HANDLE nul = CreateFile("NUL", GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE |
                          FILE_SHARE_DELETE,
                          &security_attributes, OPEN_EXISTING, 0, NULL);
  if (nul == INVALID_HANDLE_VALUE)
    Fatal("couldn't open nul");

  HANDLE stdout_read, stdout_write;
  if (!CreatePipe(&stdout_read, &stdout_write, &security_attributes, 0))
    Win32Fatal("CreatePipe");

  if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0))
    Win32Fatal("SetHandleInformation");

  PROCESS_INFORMATION process_info = {};
  STARTUPINFO startup_info = {};
  startup_info.cb = sizeof(STARTUPINFO);
  startup_info.hStdInput = nul;
  startup_info.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
  startup_info.hStdOutput = stdout_write;
  startup_info.dwFlags |= STARTF_USESTDHANDLES;

  if (!CreateProcessA(NULL, (char*)command.c_str(), NULL, NULL,
                      /* inherit handles */ TRUE, 0,
                      env_block_, NULL,
                      &startup_info, &process_info)) {
    Win32Fatal("CreateProcess");
  }

  if (!CloseHandle(nul) ||
      !CloseHandle(stdout_write)) {
    Win32Fatal("CloseHandle");
  }

  // Read all output of the subprocess.
  DWORD read_len = 1;
  while (read_len) {
    char buf[64 << 10];
    read_len = 0;
    if (!::ReadFile(stdout_read, buf, sizeof(buf), &read_len, NULL) &&
        GetLastError() != ERROR_BROKEN_PIPE) {
      Win32Fatal("ReadFile");
    }
    output->append(buf, read_len);
  }

  // Wait for it to exit and grab its exit code.
  if (WaitForSingleObject(process_info.hProcess, INFINITE) == WAIT_FAILED)
    Win32Fatal("WaitForSingleObject");
  DWORD exit_code = 0;
  if (!GetExitCodeProcess(process_info.hProcess, &exit_code))
    Win32Fatal("GetExitCodeProcess");

  if (!CloseHandle(stdout_read) ||
      !CloseHandle(process_info.hProcess) ||
      !CloseHandle(process_info.hThread)) {
    Win32Fatal("CloseHandle");
  }

  return exit_code;
}
