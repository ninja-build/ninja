#include "build_log.h"

#include "test.h"

struct BuildLogTest : public StateTestWithBuiltinRules {
  virtual void SetUp() {
    log_filename_ = tempnam(NULL, "ninja");
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

TEST_F(BuildLogTest, DoubleEntry) {
  FILE* f = fopen(log_filename_.c_str(), "wb");
  fprintf(f, "0 out command abc\n");
  fprintf(f, "0 out command def\n");
  fclose(f);

  string err;
  BuildLog log;
  EXPECT_TRUE(log.Load(log_filename_, &err));
  ASSERT_EQ("", err);

  BuildLog::LogEntry* e = log.LookupByOutput("out");
  ASSERT_TRUE(e);
  ASSERT_EQ("command def", e->command);
}
