#include "ninja/logger.h"

#include <iostream>

#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x4
#endif
#else
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/time.h>
#endif

#include "util.h"

namespace ninja {
namespace {
const char kLogError[] = "ninja: error: ";
const char kLogInfo[] = "ninja: ";
const char kLogWarning[] = "ninja: warning: ";
}  // namespace

Logger::Logger() :
  is_console_locked_(false) {}

void Logger::OnMessage(Logger::Level level, const std::string& message) {
    const char* prefix = kLogError;
    if(level == Logger::Level::INFO) {
      prefix = kLogInfo;
    }
    else if(level == Logger::Level::WARNING) {
      prefix = kLogWarning;
    }
    cerr() << prefix << message << std::endl;
}

void Logger::SetConsoleLocked(bool is_locked) {
  if (is_locked == is_console_locked_) {
    return;
  }

  if (is_locked) {
    OnConsoleLocked();
  } else {
    OnConsoleUnlocked();
  }
}

LoggerBasic::LoggerBasic() : 
  have_blank_line_(true) {
  setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

  const char* term = getenv("TERM");
#ifndef _WIN32
  is_smart_terminal_ = isatty(1) && term && std::string(term) != "dumb";
#else
  // Disable output buffer.  It'd be nice to use line buffering but
  // MSDN says: "For some systems, [_IOLBF] provides line
  // buffering. However, for Win32, the behavior is the same as _IOFBF
  // - Full Buffering."
  if (term && std::string(term) == "dumb") {
    is_smart_terminal_ = false;
  } else {
    setvbuf(stdout, NULL, _IONBF, 0);
    console_ = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    is_smart_terminal_ = GetConsoleScreenBufferInfo(console_, &csbi);
  }
#endif
  does_support_color_ = is_smart_terminal_;
  if (!does_support_color_) {
    const char* clicolor_force = getenv("CLICOLOR_FORCE");
    does_support_color_ = clicolor_force && std::string(clicolor_force) != "0";
  }
#ifdef _WIN32
  // Try enabling ANSI escape sequence support on Windows 10 terminals.
  if (does_support_color_) {
    DWORD mode;
    if (GetConsoleMode(console_, &mode)) {
      if (!SetConsoleMode(console_, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        does_support_color_ = false;
      }
    }
  }
#endif
}

void LoggerBasic::PrintStatusLine(StatusLineType type, const std::string& to_print) {
  if (is_console_locked_) {
    line_buffer_ = to_print;
    line_type_ = type;
    return;
  }

  if (is_smart_terminal_) {
    cout() << "\r";  // Print over previous line, if any.
  }

  std::string will_print = to_print;
  if (is_smart_terminal_ && type == ELIDE) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(console_, &csbi);

    will_print = ElideMiddle(will_print, static_cast<size_t>(csbi.dwSize.X));
    // We don't want to have the cursor spamming back and forth, so instead of
    // print use WriteConsoleOutput which updates the contents of the buffer,
    // but doesn't move the cursor position.
    COORD buf_size = { csbi.dwSize.X, 1};
    COORD zero_zero = { 0, 0 };
    SMALL_RECT target = {
      csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y,
      static_cast<SHORT>(csbi.dwCursorPosition.X + csbi.dwSize.X - 1),
      csbi.dwCursorPosition.Y
    };
    vecotr<CHAR_INFO> char_data(csbi.dwSize.X);
    for (size_t i = 0; i < static_cast<size_t>(csbi.dwSize.X); ++i) {
      char_data[i].Char.AsciiChar = i < will_print.size() ? will_print[i] : ' ';
      char_data[i].Attributes = csbi.wAttributes;
    }
    WriteConsoleOutput(console_, &char_data[0], buf_size, zero_zero, &target);
#else
    // Limit output to width of the terminal if provided so we don't cause
    // line-wrapping.
    winsize size;
    if ((ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) && size.ws_col) {
      will_print = ElideMiddle(will_print, size.ws_col);
    }
#endif
    cout() << will_print
      << ("\x1B[K")  // Clear to end of line.
      << std::flush;

    have_blank_line_ = false;
  } else {
    cout() << to_print << std::endl;
  }
}

void LoggerBasic::PrintStatusOnNewLine(const std::string& to_print) {
  if (is_console_locked_ && !line_buffer_.empty()) {
    output_buffer_.append(line_buffer_);
    output_buffer_.append(1, '\n');
    line_buffer_.clear();
  }
  if (!have_blank_line_) {
    PrintOrBuffer("\n", 1);
  }
  if (!to_print.empty()) {
    PrintOrBuffer(&to_print[0], to_print.size());
  }
  have_blank_line_ = to_print.empty() || *to_print.rbegin() == '\n';
}

std::ostream& LoggerBasic::cout() {
  return std::cout;
}

std::ostream& LoggerBasic::cerr() {
  return std::cerr;
}

void LoggerBasic::PrintOrBuffer(const char* data, size_t size) {
  if (is_console_locked_) {
    output_buffer_.append(data, size);
  } else {
    for (size_t i = 0; i < size; ++i) {
      cout() << data[i];
    }
    cout() << std::flush;
  }
}

void LoggerBasic::OnConsoleLocked() {
  PrintStatusOnNewLine("");
}

void LoggerBasic::OnConsoleUnlocked() {
  PrintStatusOnNewLine(output_buffer_);
  if (!line_buffer_.empty()) {
    PrintStatusLine(line_type_, line_buffer_);
  }
  output_buffer_.clear();
  line_buffer_.clear();
}

LoggerNull::LoggerNull() :
  null_buffer(new NullBuffer()),
  null_stream(null_buffer) {}

LoggerNull::~LoggerNull() {
  delete null_buffer;
}

std::ostream& LoggerNull::cerr() {
  return null_stream;
}
std::ostream& LoggerNull::cout() {
  return null_stream;
}

}  // namespace ninja
