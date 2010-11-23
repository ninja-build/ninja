#include "build.h"

#include <gtest/gtest.h>

TEST(Subprocess, Ls) {
  Subprocess ls;
  string err;
  EXPECT_TRUE(ls.Start("ls /", &err));
  ASSERT_EQ("", err);

  // Pretend we discovered that stdout was ready for writing.
  ls.OnFDReady(ls.stdout_.fd_);

  EXPECT_TRUE(ls.Finish(&err));
  ASSERT_EQ("", err);
  EXPECT_NE("", ls.stdout_.buf_);
}
