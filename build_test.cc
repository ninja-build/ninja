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
  EXPECT_EQ("", ls.stderr_.buf_);
}

TEST(Subprocess, BadCommand) {
  Subprocess subproc;
  string err;
  EXPECT_TRUE(subproc.Start("ninja_no_such_command", &err));
  ASSERT_EQ("", err);

  // Pretend we discovered that stderr was ready for writing.
  subproc.OnFDReady(subproc.stderr_.fd_);

  EXPECT_FALSE(subproc.Finish(&err));
  EXPECT_NE("", err);
  EXPECT_EQ("", subproc.stdout_.buf_);
  EXPECT_NE("", subproc.stderr_.buf_);
}

TEST(SubprocessSet, Single) {
  SubprocessSet subprocs;
  Subprocess* ls = new Subprocess;
  string err;
  EXPECT_TRUE(ls->Start("ls /", &err));
  ASSERT_EQ("", err);
  subprocs.Add(ls);

  while (!ls->done()) {
    subprocs.DoWork(&err);
    ASSERT_EQ("", err);
  }
  ASSERT_NE("", ls->stdout_.buf_);
}
