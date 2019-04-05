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

//#include "graph.h"
//#include "state.h"
#include "line_printer.h"
#include "test.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#define PUTENV(name) _putenv(name)
#else
#include <unistd.h>
#define PUTENV(name) putenv(name)
#endif

TEST(LinePrinterTest, DumbTermEnv) {
  bool actually_smart = false;
#ifndef _WIN32
  const char* term = getenv("TERM");
  actually_smart = isatty(1) && term && string(term) != "dumb";
#else
  setvbuf(stdout, NULL, _IONBF, 0);
  void* console = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  actually_smart = GetConsoleScreenBufferInfo(console, &csbi);
#endif

  if(actually_smart){
    LinePrinter line_printer;
    PUTENV("TERM=");
    line_printer = LinePrinter();
    EXPECT_TRUE(line_printer.is_smart_terminal());
    PUTENV("TERM=notdumb");
    line_printer = LinePrinter();
    EXPECT_TRUE(line_printer.is_smart_terminal());
    PUTENV("TERM=dumb");
    line_printer = LinePrinter();
    EXPECT_FALSE(line_printer.is_smart_terminal());
  }
}