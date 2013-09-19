// Copyright 2013 Google Inc. All Rights Reserved.
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

#include "line_printer.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#endif

#include "util.h"

#ifdef _WIN32
static bool IsCygwinTTY(HANDLE Handle);
#endif

LinePrinter::LinePrinter() : have_blank_line_(true), terminal_type_(TERM_DUMB) {
#ifndef _WIN32
  const char* term = getenv("TERM");
  bool smart = isatty(1) && term && string(term) != "dumb";
  terminal_type_ = smart ? TERM_ANSI : TERM_DUMB;
#else
  // Disable output buffer.  It'd be nice to use line buffering but
  // MSDN says: "For some systems, [_IOLBF] provides line
  // buffering. However, for Win32, the behavior is the same as _IOFBF
  // - Full Buffering."
  setvbuf(stdout, NULL, _IONBF, 0);
  console_ = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  bool is_cmd = GetConsoleScreenBufferInfo(console_, &csbi);
  if (is_cmd) {
    terminal_type_ = TERM_CMD;
  } else if (IsCygwinTTY(console_)) {
    // Cygwin uses mintty by default these days, and it understands ANSI codes.
    terminal_type_ = TERM_ANSI;
  }
#endif
}

void LinePrinter::Print(string to_print, LineType type) {
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(console_, &csbi);
#endif

#ifdef _WIN32
  if (terminal_type_ == TERM_CMD) {
    csbi.dwCursorPosition.X = 0;
    SetConsoleCursorPosition(console_, csbi.dwCursorPosition);
  }
#endif
  if (terminal_type_ == TERM_ANSI)
    printf("\r");  // Print over previous line, if any.

#ifdef _WIN32
  if (terminal_type_ == TERM_CMD && type == ELIDE) {
    // Don't use the full width or console will move to next line.
    size_t width = static_cast<size_t>(csbi.dwSize.X) - 1;
    to_print = ElideMiddle(to_print, width);
    // We don't want to have the cursor spamming back and forth, so
    // use WriteConsoleOutput instead which updates the contents of
    // the buffer, but doesn't move the cursor position.
    GetConsoleScreenBufferInfo(console_, &csbi);
    COORD buf_size = { csbi.dwSize.X, 1 };
    COORD zero_zero = { 0, 0 };
    SMALL_RECT target = {
      csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y,
      static_cast<SHORT>(csbi.dwCursorPosition.X + csbi.dwSize.X - 1),
      csbi.dwCursorPosition.Y
    };
    CHAR_INFO* char_data = new CHAR_INFO[csbi.dwSize.X];
    memset(char_data, 0, sizeof(CHAR_INFO) * csbi.dwSize.X);
    for (int i = 0; i < csbi.dwSize.X; ++i) {
      char_data[i].Char.AsciiChar = ' ';
      char_data[i].Attributes = csbi.wAttributes;
    }
    for (size_t i = 0; i < to_print.size(); ++i)
      char_data[i].Char.AsciiChar = to_print[i];
    WriteConsoleOutput(console_, char_data, buf_size, zero_zero, &target);
    delete[] char_data;
  }
#endif
  if (terminal_type_ == TERM_ANSI && type == ELIDE) {
#ifndef _WIN32
    // Limit output to width of the terminal if provided so we don't cause
    // line-wrapping.
    winsize size;
    if ((ioctl(0, TIOCGWINSZ, &size) == 0) && size.ws_col) {
      to_print = ElideMiddle(to_print, size.ws_col);
    }
#endif
    printf("%s", to_print.c_str());
    printf("\x1B[K");  // Clear to end of line.
    fflush(stdout);
  }

  if (terminal_type_ != TERM_DUMB && type == ELIDE) {
    have_blank_line_ = false;
  } else {
    printf("%s\n", to_print.c_str());
  }
}

void LinePrinter::PrintOnNewLine(const string& to_print) {
  if (!have_blank_line_)
    printf("\n");
  if (!to_print.empty()) {
    // Avoid printf and C strings, since the actual output might contain null
    // bytes like UTF-16 does (yuck).
    fwrite(&to_print[0], sizeof(char), to_print.size(), stdout);
  }
  have_blank_line_ = to_print.empty() || *to_print.rbegin() == '\n';
}

#ifdef _WIN32
// Hide these types in an anonymous namespace so we don't conflict with whatever
// comes out of windows.h.
namespace {

struct UNICODE_STRING {
  USHORT Length;
  USHORT MaximumLength;
  PWSTR  Buffer;
};

// This is a custom definition.
struct OBJECT_NAME_INFORMATION {
  UNICODE_STRING          Name;
  WCHAR                   NameBuffer[MAX_PATH];
};

enum OBJECT_INFORMATION_CLASS { ObjectNameInformation = 1 };

typedef NTSTATUS (__stdcall *NtQueryObjectType)(
    /* IN  */ HANDLE ObjectHandle,
    /* IN  */ OBJECT_INFORMATION_CLASS ObjectInformationClass,
    /* OUT */ PVOID ObjectInformation,
    /* IN  */ ULONG ObjectInformationLength,
    /* OUT */ ULONG *ReturnLength);

}

// Figures out if the given handle points to a pipe pty created by a Cygwin
// shell.  Cygwin sets stdout to a named pipe with a name in a parsable format,
// which is how it later decides if stdout is a tty or not.
// TODO: Make this work for MSys bash.
static bool IsCygwinTTY(HANDLE handle) {
  // Use GetProcAddress to find NtQueryObject so we don't have to link against
  // ntdll directly, which is only present in the WDK or Win8 SDK.
  HMODULE mod = LoadLibrary("ntdll");
  if (!mod)
    return false;
  NtQueryObjectType query =
      NtQueryObjectType(GetProcAddress(mod, "NtQueryObject"));
  if (!query)
    return false;

  // The result is a UNICODE_STRING where the storage for the name directly
  // follows the struct.  We pass the size of the whole thing into
  // NtQueryObject.
  OBJECT_NAME_INFORMATION name_info;
  ULONG len;
  NTSTATUS status = query(handle, ObjectNameInformation, (void *)&name_info,
                          sizeof(name_info), &len);
  if (status != 0)
    return false;

  // If the handle represents a named pipe starting with "cygwin-", this was
  // created by Cygwin.
  wchar_t pty_prefix[] = L"\\Device\\NamedPipe\\cygwin-";
  if (wcsncmp(name_info.NameBuffer, pty_prefix, wcslen(pty_prefix)) != 0)
    return false;

  // If it also has "-pty" in it, that's a strong signal that it's a pseudo-tty.
  return wcsstr(name_info.NameBuffer, L"-pty");
}

#endif
