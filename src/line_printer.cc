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

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#ifdef _MSC_VER
#include <windows.h>
#else
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/time.h>
#endif

#include "util.h"

LinePrinter::LinePrinter() : dirty_height(0), console_locked_(false) {
#ifndef _MSC_VER
  const char* term = getenv("TERM");
  smart_terminal_ = isatty(1) && term && string(term) != "dumb";
  winsize size;
  if ((ioctl(0, TIOCGWINSZ, &size) == 0) && size.ws_col) {
    console_width = size.ws_col;
    console_height = size.ws_row;
  } else {
    smart_terminal_ = false;  // Temporary prints are not possible more than on one line
  }
#else
  // Disable output buffer.  It'd be nice to use line buffering but
  // MSDN says: "For some systems, [_IOLBF] provides line
  // buffering. However, for Win32, the behavior is the same as _IOFBF
  // - Full Buffering."
  setvbuf(stdout, NULL, _IONBF, 0);
  console_ = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  smart_terminal_ = GetConsoleScreenBufferInfo(console_, &csbi);
  bottom_dirty_row = 0;
  console_height = static_cast<size_t>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
#endif
}

void LinePrinter::PrintTemporaryElide() {
  vector<string> to_print_lines;
  PrintTemporaryElide(to_print_lines);
}

void LinePrinter::PrintTemporaryElide(const string& to_print) {
  vector<string> to_print_lines;
  if (!to_print.empty())
    to_print_lines.push_back(to_print);
  PrintTemporaryElide(to_print_lines);
}

void LinePrinter::PrintTemporaryElide(const vector<string>& to_print) {
  if (console_locked_)
    return;

  if (!smart_terminal_)
    return;

#ifdef _MSC_VER
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(console_, &csbi);
  int old_console_height = console_height;
  console_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#else
  winsize size;
  if ((ioctl(0, TIOCGWINSZ, &size) == 0) && size.ws_col) {
    console_width = size.ws_col;
    console_height = size.ws_row;
  }
#endif

  // How much to print?
  // Try to keep some lines above us so that the user notices
  // when other output is printed in between these status lines.
  bool print_ellipsis = false;
  int lines_above_us = max(1, console_height / 2); // Tunable factor.
  int lines_to_print;
  if (console_height < lines_above_us + 1) {
    // Print nothing or the first line only.
    lines_above_us = 0;
    lines_to_print = min(static_cast<int>(to_print.size()), console_height);
  } else if (console_height < lines_above_us + static_cast<int>(to_print.size())) {
    // Keep one line, fill the rest of the screen,
    // but replace the last line with "..."
    lines_to_print = console_height - 1 - lines_above_us;
    print_ellipsis = true;
  } else {
    lines_to_print = static_cast<int>(to_print.size());
  }
  int old_dirty_height = dirty_height;
  dirty_height = lines_to_print + (print_ellipsis ? 1 : 0);

#ifdef _MSC_VER
  // Scroll the buffer, on Windows terminal, if overfilling.

  COORD zero_zero = { 0, 0 };
  // Fix the old bottom_dirty_row according to possible changed buffer size.
  if (old_dirty_height == 0) {
    // No temporary table exists, print at the cursor position.
    bottom_dirty_row = csbi.dwCursorPosition.Y;
  } else {
    if (bottom_dirty_row >= csbi.dwSize.Y) {
      // The buffer height have been smaller and the top rows have been
      // removed to keep the latest printed stuff. Update bottom_dirty_row.
      bottom_dirty_row = max(csbi.dwSize.Y - 1, 0);
    }
  }
  // top_dirty_row is within [0; bottom_dirty_row].
  size_t top_dirty_row = max(bottom_dirty_row - max(old_dirty_height - 1, 0), 0);
  // Set the new dirty bottom row
  bottom_dirty_row = top_dirty_row + max(dirty_height - 1, 0);

  // If there are not enough rows left, scroll the buffer.
  if (bottom_dirty_row >= csbi.dwSize.Y) {
    int lines_to_move_up = bottom_dirty_row - max(csbi.dwSize.Y - 1, 0);
    top_dirty_row -= lines_to_move_up;
    bottom_dirty_row -= lines_to_move_up;

    SMALL_RECT content_to_scroll_rect = {
      0,
      static_cast<SHORT>(lines_to_move_up),
      csbi.dwSize.X - 1,
      csbi.dwSize.Y - 1,
    };
    SMALL_RECT clip_rect = {
      0,
      0,
      csbi.dwSize.X - 1,
      csbi.dwSize.Y - 1
    };
    CHAR_INFO fill_char;
    fill_char.Char.AsciiChar = (char)' ';
    fill_char.Attributes = csbi.wAttributes;

    ScrollConsoleScreenBuffer(console_, &content_to_scroll_rect, &clip_rect, zero_zero, &fill_char);
  }

  // If the console has become smaller, we will print less, so scroll up a bit first.
  if (console_height < old_console_height && csbi.srWindow.Bottom > bottom_dirty_row) {
    // Scroll up to make the first row visible.
    SHORT lines_removed = static_cast<SHORT>(max(dirty_height, old_dirty_height) - dirty_height);
    // Adjust csbi.srWindow directly as it may be used later as well.
    csbi.srWindow.Top -= lines_removed;
    csbi.srWindow.Bottom -= lines_removed;
    SetConsoleWindowInfo(console_, TRUE, &csbi.srWindow);
  }

  // If the amount of rows printed have changed, set the cursor to the end row.
  if (dirty_height > old_dirty_height || dirty_height == 0) {
    // Place the cursor at the end.
    COORD bottom_left_pos = {
      0, // Scroll to the far left.
      static_cast<SHORT>(bottom_dirty_row),
    };
    SetConsoleCursorPosition(console_, bottom_left_pos);
  } else {
    // Avoid scrolling when using printf() below.
    COORD pos = {
      csbi.srWindow.Left,
      static_cast<SHORT>(bottom_dirty_row),
    };
    if (pos.Y <= csbi.srWindow.Top)
      pos.Y = csbi.srWindow.Top;
    if (pos.Y >= csbi.srWindow.Bottom)
      pos.Y = csbi.srWindow.Bottom;
    SetConsoleCursorPosition(console_, pos);
  }
  // On Windows, calling a C library function writing to stdout also handles
  // pausing the executable when the "Pause" key or Ctrl-S is pressed.
  printf("\r");

  // We don't want to have the cursor spamming back and forth, so instead of
  // printf use WriteConsoleOutput which updates the contents of the buffer,
  // but doesn't move the cursor position.
  COORD buf_size = { csbi.dwSize.X, static_cast<SHORT>(max(dirty_height, old_dirty_height)) };
  if (buf_size.Y > 0) {
    SMALL_RECT target = {
      0,
      static_cast<SHORT>(top_dirty_row),
      static_cast<SHORT>(buf_size.X - 1),
      static_cast<SHORT>(top_dirty_row + buf_size.Y - 1)
    };
    vector<CHAR_INFO> char_data(buf_size.Y * buf_size.X);
    for (int y = 0; y < buf_size.Y; ++y) {
      string to_print_line;
      if (y < lines_to_print) {
        to_print_line = ElideMiddle(to_print[y], static_cast<size_t>(buf_size.X));
      } else if (y == lines_to_print && print_ellipsis) {
        to_print_line = "...";
      }
      for (int x = 0; x < buf_size.X; ++x) {
        int i = y*buf_size.X + x;
        char_data[i].Char.AsciiChar = static_cast<size_t>(x) < to_print_line.size() ? to_print_line[x] : ' ';
        char_data[i].Attributes = csbi.wAttributes;
      }
    }
    WriteConsoleOutput(console_, char_data.data(), buf_size, zero_zero, &target);
  }
#else
  printf("\r");  // Go to beginning of the line

  bool first_line_up = true;
  if (old_dirty_height > dirty_height) {
    for (int i = old_dirty_height - 1; i >= dirty_height; --i) {
      if (first_line_up)
        first_line_up = false;
      else
        printf("\x1B[A");  // Go up a line
      printf("\x1B[2K");  // Clear that line
    }
  }
  // Lines to print will clear themselves later. Go up some more lines, without clearing them.
  int lines_to_go_up = max(min(dirty_height, old_dirty_height) - (first_line_up ? 1 : 0), 0);
  if (lines_to_go_up > 0)
    printf("\x1B[%dA", lines_to_go_up);

  // Limit output to width of the terminal if provided so we don't cause
  // line-wrapping.
  for (int i = 0; i < dirty_height; ++i) {
    string to_print_line = (i < lines_to_print ? ElideMiddle(to_print[i], console_width) : "...");
    printf("%s", to_print_line.c_str());
    printf("\x1B[K");  // Clear to end of line.
    if (i < dirty_height-1)
      printf("\n");
  }
  fflush(stdout);
#endif
}

void LinePrinter::PrintOrBuffer(const char* data, size_t size) {
  if (console_locked_) {
    output_buffer_.append(data, size);
  } else {
    // Avoid printf and C strings, since the actual output might contain null
    // bytes like UTF-16 does (yuck).
    fwrite(data, 1, size, stdout);
  }
}

void LinePrinter::Print(const string& to_print) {
  if (dirty_height > 0)
    PrintTemporaryElide();
  PrintOrBuffer(to_print.data(), to_print.size());
}

void LinePrinter::SetConsoleLocked(bool locked) {
  if (locked == console_locked_)
    return;

  if (locked)
    PrintTemporaryElide();

  console_locked_ = locked;

  if (!locked) {
    Print(output_buffer_);
    output_buffer_.clear();
  }
}
