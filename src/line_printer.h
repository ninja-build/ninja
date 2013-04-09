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

class LinePrinter {
 public:
  LinePrinter();

  enum LineType {
    FULL,
    SHORT
  };
  void Print(std::string to_print, LineType type);

 //private:
  /// Whether we can do fancy terminal control codes.
  bool smart_terminal_;

  bool have_blank_line_;
};

#endif  // NINJA_LINE_PRINTER_H_
