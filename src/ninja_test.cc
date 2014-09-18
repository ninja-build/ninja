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

#ifdef _WIN32
#include "getopt.h"
#else
#include <getopt.h>
#endif

#include "test.h"
#include "line_printer.h"

struct RegisteredTest {
  testing::Test* (*factory)();
  const char *name;
  bool should_run;
};
// This can't be a vector because tests call RegisterTest from static
// initializers and the order static initializers run it isn't specified. So
// the vector constructor isn't guaranteed to run before all of the
// RegisterTest() calls.
static RegisteredTest tests[10000];
testing::Test* g_current_test;
static int ntests;
static LinePrinter printer;

void RegisterTest(testing::Test* (*factory)(), const char* name) {
  tests[ntests].factory = factory;
  tests[ntests++].name = name;
}

namespace {
string StringPrintf(const char* format, ...) {
  const int N = 1024;
  char buf[N];

  va_list ap;
  va_start(ap, format);
  vsnprintf(buf, N, format, ap);
  va_end(ap);

  return buf;
}

void Usage() {
  fprintf(stderr,
"usage: ninja_tests [options]\n"
"\n"
"options:\n"
"  --gtest_filter=POSTIVE_PATTERN[-NEGATIVE_PATTERN]\n"
"      Run tests whose names match the positive but not the negative pattern.\n"
"      '*' matches any substring. (gtest's ':', '?' are not implemented).\n");
}

bool PatternMatchesString(const char* pattern, const char* str) {
  switch (*pattern) {
    case '\0':
    case '-': return *str == '\0';
    case '*': return (*str != '\0' && PatternMatchesString(pattern, str + 1)) ||
                     PatternMatchesString(pattern + 1, str);
    default:  return *pattern == *str &&
                     PatternMatchesString(pattern + 1, str + 1);
  }
}

bool TestMatchesFilter(const char* test, const char* filter) {
  // Split --gtest_filter at '-' into positive and negative filters.
  const char* const dash = strchr(filter, '-');
  const char* pos = dash == filter ? "*" : filter; //Treat '-test1' as '*-test1'
  const char* neg = dash ? dash + 1 : "";
  return PatternMatchesString(pos, test) && !PatternMatchesString(neg, test);
}

bool ReadFlags(int* argc, char*** argv, const char** test_filter) {
  enum { OPT_GTEST_FILTER = 1 };
  const option kLongOptions[] = {
    { "gtest_filter", required_argument, NULL, OPT_GTEST_FILTER },
    { NULL, 0, NULL, 0 }
  };

  int opt;
  while ((opt = getopt_long(*argc, *argv, "h", kLongOptions, NULL)) != -1) {
    switch (opt) {
    case OPT_GTEST_FILTER:
      if (strchr(optarg, '?') == NULL && strchr(optarg, ':') == NULL) {
        *test_filter = optarg;
        break;
      }  // else fall through.
    default:
      Usage();
      return false;
    }
  }
  *argv += optind;
  *argc -= optind;
  return true;
}

}  // namespace

bool testing::Test::Check(bool condition, const char* file, int line,
                          const char* error) {
  if (!condition) {
    printer.PrintOnNewLine(
        StringPrintf("*** Failure in %s:%d\n%s\n", file, line, error));
    failed_ = true;
  }
  return condition;
}

int main(int argc, char **argv) {
  int tests_started = 0;

  const char* test_filter = "*";
  if (!ReadFlags(&argc, &argv, &test_filter))
    return 1;

  int nactivetests = 0;
  for (int i = 0; i < ntests; i++)
    if ((tests[i].should_run = TestMatchesFilter(tests[i].name, test_filter)))
      ++nactivetests;

  bool passed = true;
  for (int i = 0; i < ntests; i++) {
    if (!tests[i].should_run) continue;

    ++tests_started;
    testing::Test* test = tests[i].factory();
    printer.Print(
        StringPrintf("[%d/%d] %s", tests_started, nactivetests, tests[i].name),
        LinePrinter::ELIDE);
    test->SetUp();
    test->Run();
    test->TearDown();
    if (test->Failed())
      passed = false;
    delete test;
  }

  printer.PrintOnNewLine(passed ? "passed\n" : "failed\n");
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
