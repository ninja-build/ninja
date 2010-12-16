#include "build_log.h"

#include "test.h"

struct BuildLogTest : public StateTestWithBuiltinRules {
  virtual void SetUp() {
    char mktemp_template[] = "BuildLogTest-XXXXXX";
    log_filename_ = mktemp(mktemp_template);
  }
  virtual void TearDown() {
    unlink(log_filename_.c_str());
  }

  string log_filename_;
};

TEST_F(BuildLogTest, WriteRead) {
  AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n");

  BuildLog log1;
  string err;
  EXPECT_TRUE(log1.OpenForWrite(log_filename_, &err));
  ASSERT_EQ("", err);
  log1.RecordCommand(state_.edges_[0], 15);
  log1.RecordCommand(state_.edges_[1], 20);
  log1.Close();

  BuildLog log2;
  EXPECT_TRUE(log2.Load(log_filename_, &err));
  ASSERT_EQ("", err);

  ASSERT_EQ(2, log1.log_.size());
  ASSERT_EQ(2, log2.log_.size());
  BuildLog::LogEntry* e1 = log1.LookupByOutput("out");
  ASSERT_TRUE(e1);
  BuildLog::LogEntry* e2 = log2.LookupByOutput("out");
  ASSERT_TRUE(e2);
  ASSERT_TRUE(*e1 == *e2);
  ASSERT_EQ(15, e1->time_ms);
  ASSERT_EQ("out", e1->output);
}
