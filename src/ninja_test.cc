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

#include <stdarg.h>
#include <stdio.h>

#include "gtest/gtest.h"
#include "line_printer.h"

string StringPrintf(const char* format, ...) {
  const int N = 1024;
  char buf[N];

  va_list ap;
  va_start(ap, format);
  vsnprintf(buf, N, format, ap);
  va_end(ap);

  return buf;
}

/// A test result printer that's less wordy than gtest's default.
struct LaconicPrinter : public testing::EmptyTestEventListener {
  LaconicPrinter() : tests_started_(0), test_count_(0), iteration_(0) {}
  virtual void OnTestProgramStart(const testing::UnitTest& unit_test) {
    test_count_ = unit_test.test_to_run_count();
  }

  virtual void OnTestIterationStart(const testing::UnitTest& test_info,
                                    int iteration) {
    tests_started_ = 0;
    iteration_ = iteration;
  }

  virtual void OnTestStart(const testing::TestInfo& test_info) {
    ++tests_started_;
    printer_.Print(
        StringPrintf("[%d/%d%s] %s.%s",
                     tests_started_,
                     test_count_,
                     iteration_ ? StringPrintf(" iter %d", iteration_).c_str()
                                : "",
                     test_info.test_case_name(),
                     test_info.name()),
        LinePrinter::ELIDE);
  }

  virtual void OnTestPartResult(
      const testing::TestPartResult& test_part_result) {
    if (!test_part_result.failed())
      return;
    printer_.PrintOnNewLine(StringPrintf(
        "*** Failure in %s:%d\n%s\n", test_part_result.file_name(),
        test_part_result.line_number(), test_part_result.summary()));
  }

  virtual void OnTestProgramEnd(const testing::UnitTest& unit_test) {
    printer_.PrintOnNewLine(unit_test.Passed() ? "passed\n" : "failed\n");
  }

 private:
  LinePrinter printer_;
  int tests_started_;
  int test_count_;
  int iteration_;
};

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  testing::TestEventListeners& listeners =
      testing::UnitTest::GetInstance()->listeners();
  delete listeners.Release(listeners.default_result_printer());
  listeners.Append(new LaconicPrinter);

  return RUN_ALL_TESTS();
}
