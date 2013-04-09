#include "gtest/gtest.h"

/// A test result printer that's less wordy than gtest's default.
class LaconicPrinter : public testing::EmptyTestEventListener {
 public:
  LaconicPrinter() : have_blank_line_(false), smart_terminal_(true) {}

  virtual void OnTestStart(const testing::TestInfo& test_info) {
    printf("\r%s.%s starting.", test_info.test_case_name(), test_info.name());
    printf("\x1B[K");  // Clear to end of line.
    fflush(stdout);
    have_blank_line_ = false;
  }

  virtual void OnTestPartResult(
      const testing::TestPartResult& test_part_result) {
    if (!test_part_result.failed())
      return;
    if (!have_blank_line_ && smart_terminal_)
      printf("\n");
    printf("*** Failure in %s:%d\n%s\n",
           test_part_result.file_name(),
           test_part_result.line_number(),
           test_part_result.summary());
    have_blank_line_ = true;
  }

  virtual void OnTestEnd(const testing::TestInfo& test_info) {
    printf("\r%s.%s ending.", test_info.test_case_name(), test_info.name());
    printf("\x1B[K");  // Clear to end of line.
    fflush(stdout);
    have_blank_line_ = false;
  }

  virtual void OnTestProgramEnd(const testing::UnitTest& unit_test) {
    if (!have_blank_line_ && smart_terminal_)
      printf("\n");
  }

 private:
  bool have_blank_line_;
  bool smart_terminal_;
};

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  testing::TestEventListeners& listeners =
      testing::UnitTest::GetInstance()->listeners();
  delete listeners.Release(listeners.default_result_printer());
  listeners.Append(new LaconicPrinter);

  return RUN_ALL_TESTS();
}
