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

#ifndef NINJA_LINE_PRINTER_H_
#define NINJA_LINE_PRINTER_H_

#include <string>
using namespace std;

/// Prints lines of text, possibly overprinting previously printed lines
/// if the terminal supports it.
struct LinePrinter {
  LinePrinter();

  enum LineType {
    FULL,
    ELIDE
  };

  enum SmartTerminalMode {
    SMART_TERMINAL_OFF,
    SMART_TERMINAL_ON,
    SMART_TERMINAL_HIDE_NEWLINE,
  };

  bool is_smart_terminal() const {
    return ( smart_terminal_ == SMART_TERMINAL_ON || smart_terminal_ == SMART_TERMINAL_HIDE_NEWLINE );
  }
  SmartTerminalMode get_smart_terminal_mode() const { return smart_terminal_; }
  void set_smart_terminal(SmartTerminalMode mode) { smart_terminal_ = mode; }

  /// Overprints the current line. If type is ELIDE, elides to_print to fit on
  /// one line.
  void Print(string to_print, LineType type);

  /// Prints a string on a new line, not overprinting previous output.
  void PrintOnNewLine(const string& to_print);

 private:
  /// Whether we can do fancy terminal control codes.
  SmartTerminalMode smart_terminal_;

  /// Whether the caret is at the beginning of a blank line.
  bool have_blank_line_;

#ifdef _WIN32
  void* console_;
#endif
};

#endif  // NINJA_LINE_PRINTER_H_
