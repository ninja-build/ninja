// Copyright 2019 Google Inc. All Rights Reserved.
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
#ifndef NINJA_PUBLIC_LOGGER_H_
#define NINJA_PUBLIC_LOGGER_H_

#include <ostream>
#include <string>
#include <utility>

namespace ninja {

class Logger {
public:
  explicit Logger();

  enum Level {
    ERROR = 0,
    WARNING = 1,
    INFO = 2
  };

  enum StatusLineType {
    FULL, // print the entire line regardless of length
    ELIDE, // elide the middle of the line to make it fit
  };

  /// Set whether or not the console should be locked. This should prevent
  /// the logger from writing to stdout/stderr until the console is unlocked
  /// which is important for handling certain subprocesses that may require
  /// exclusive access to those streams.
  void SetConsoleLocked(bool is_locked);

  /// Return whether or not this logger can handle VT100 color escape codes.
  virtual bool DoesSupportColor() const { return false; }
  /// Return whether or not this logger is 'smart'.
  virtual bool IsSmartTerminal() const { return false; }
  /// Function to handle a discrete message for stderr.
  virtual void OnMessage(Level level, const std::string& message);

  /// Overprints the current line. If type is ELIDE, elides to_print to fit on
  /// one line.
  virtual void PrintStatusLine(StatusLineType type, const std::string& to_print) = 0;
  virtual void PrintStatusOnNewLine(const std::string& to_print) = 0;

  inline void Error(const std::string& message) {
    OnMessage(ERROR, message);
  }
  inline void Warning(const std::string& message) {
    OnMessage(WARNING, message);
  }
  inline void Info(const std::string& message) {
    OnMessage(INFO, message);
  }

  virtual std::ostream& cout() = 0;
  virtual std::ostream& cerr() = 0;

protected:
  virtual void OnConsoleLocked() {};
  virtual void OnConsoleUnlocked() {};

  bool is_console_locked_;
 
};

class LoggerBasic : public Logger {
public:
  explicit LoggerBasic();
  virtual bool DoesSupportColor() const { return does_support_color_; }
  virtual bool IsSmartTerminal() const { return is_smart_terminal_; }
  virtual void PrintStatusLine(StatusLineType type, const std::string& to_print) override;
  virtual void PrintStatusOnNewLine(const std::string& to_print) override;

  virtual std::ostream& cerr() override;
  virtual std::ostream& cout() override;

protected:
  void PrintOrBuffer(const char* data, size_t size);

  virtual void OnConsoleLocked() override;
  virtual void OnConsoleUnlocked() override;

  /// Whether we can use ISO 6429 (ANSI) color sequences.
  bool does_support_color_;

  /// Whether or not the terminal is 'smart' - which practically
  /// means that it will handle carriage returns properly.
  bool is_smart_terminal_;

  /// Whether the caret is at the beginning of a blank line.
  bool have_blank_line_;

  /// Buffered current line while console is locked.
  std::string line_buffer_;

  /// Buffered line type while console is locked.
  StatusLineType line_type_;

  /// Buffered console output while console is locked.
  std::string output_buffer_;

#ifdef _WIN32
  void* console_;
#endif
};

class NullBuffer : public std::streambuf
{
public:
  int overflow(int c) { return c; }
};

class LoggerNull : public Logger {
public:
  LoggerNull();
  ~LoggerNull();

  virtual bool DoesSupportColor() const { return false; }
  virtual bool IsSmartTerminal() const { return false; }
  virtual std::ostream& cerr() override;
  virtual std::ostream& cout() override;
private:
  NullBuffer* null_buffer;
  std::ostream null_stream;
};


}  // namespace ninja
#endif  // NINJA_PUBLIC_LOGGER_H_
