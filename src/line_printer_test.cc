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

TEST(LinePrinterTest, DumbTermEnv) {
  LinePrinter line_printer;
  _putenv("TERM=");
  line_printer = LinePrinter();
  EXPECT_TRUE(line_printer.is_smart_terminal());
  _putenv("TERM=notdumb");
  line_printer = LinePrinter();
  EXPECT_TRUE(line_printer.is_smart_terminal());
  _putenv("TERM=dumb");
  line_printer = LinePrinter();
  EXPECT_FALSE(line_printer.is_smart_terminal());
}