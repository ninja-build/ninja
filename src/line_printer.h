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

#include <stddef.h>
#include <string>
#include <vector>
using namespace std;

/// Prints lines of text, possibly overprinting previously printed lines
/// if the terminal supports it.
struct LinePrinter {
  LinePrinter();

  bool is_smart_terminal() const { return smart_terminal_; }
  void set_smart_terminal(bool smart) { smart_terminal_ = smart; }

  /// Overprints the current box. If type is ELIDE, elides to_print to fit on
  /// one line by line.
  void PrintTemporaryElide();
  void PrintTemporaryElide(const string& to_print);
  void PrintTemporaryElide(const vector<string>& to_print);

  /// Prints a string in the normal way. Will remove any temporary output.
  void Print(const string& to_print);

  /// Lock or unlock the console.  Any output sent to the LinePrinter while the
  /// console is locked will not be printed until it is unlocked.
  void SetConsoleLocked(bool locked);

 private:
  /// Whether we can do fancy terminal control codes.
  bool smart_terminal_;

  /// How much of the terminal is dirty?
  int dirty_height;

  /// Whether console is locked.
  bool console_locked_;

  /// Buffered console output while console is locked.
  string output_buffer_;

#ifdef _MSC_VER
  void* console_;
  /// If dirty_height != 0, the bottom row where stuff got printed last time.
  int bottom_dirty_row;
#else
  int console_width;
#endif
  int console_height;

  /// Print the given data to the console, or buffer it if it is locked.
  void PrintOrBuffer(const char *data, size_t size);
};

#endif  // NINJA_LINE_PRINTER_H_
