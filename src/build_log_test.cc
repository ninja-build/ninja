// Copyright 2011 Google Inc. All Rights Reserved.
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

#include "build_log.h"

#include "test.h"

#ifdef _WIN32
#include <fcntl.h>
#include <share.h>
#endif

#ifdef linux
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static const char kTestFilename[] = "BuildLogTest-tempfile";

struct BuildLogTest : public StateTestWithBuiltinRules {
  virtual void SetUp() {
  }
  virtual void TearDown() {
    unlink(kTestFilename);
  }
};

TEST_F(BuildLogTest, WriteRead) {
  AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n");

  BuildLog log1;
  string err;
  EXPECT_TRUE(log1.OpenForWrite(kTestFilename, &err));
  ASSERT_EQ("", err);
  log1.RecordCommand(state_.edges_[0], 15, 18);
  log1.RecordCommand(state_.edges_[1], 20, 25);
  log1.Close();

  BuildLog log2;
  EXPECT_TRUE(log2.Load(kTestFilename, &err));
  ASSERT_EQ("", err);

  ASSERT_EQ(2u, log1.log().size());
  ASSERT_EQ(2u, log2.log().size());
  BuildLog::LogEntry* e1 = log1.LookupByOutput("out");
  ASSERT_TRUE(e1);
  BuildLog::LogEntry* e2 = log2.LookupByOutput("out");
  ASSERT_TRUE(e2);
  ASSERT_TRUE(*e1 == *e2);
  ASSERT_EQ(15, e1->start_time);
  ASSERT_EQ("out", e1->output);
}

TEST_F(BuildLogTest, DoubleEntry) {
  FILE* f = fopen(kTestFilename, "wb");
  fprintf(f, "# ninja log v3\n");
  fprintf(f, "0 1 2 out command abc\n");
  fprintf(f, "3 4 5 out command def\n");
  fclose(f);

  string err;
  BuildLog log;
  EXPECT_TRUE(log.Load(kTestFilename, &err));
  ASSERT_EQ("", err);

  BuildLog::LogEntry* e = log.LookupByOutput("out");
  ASSERT_TRUE(e);
  ASSERT_EQ("command def", e->command);
}

TEST_F(BuildLogTest, Truncate) {
  AssertParse(&state_,
"build out: cat mid\n"
"build mid: cat in\n");

  BuildLog log1;
  string err;
  EXPECT_TRUE(log1.OpenForWrite(kTestFilename, &err));
  ASSERT_EQ("", err);
  log1.RecordCommand(state_.edges_[0], 15, 18);
  log1.RecordCommand(state_.edges_[1], 20, 25);
  log1.Close();

  struct stat statbuf;
  ASSERT_EQ(0, stat(kTestFilename, &statbuf));
  ASSERT_GT(statbuf.st_size, 0);

  // For all possible truncations of the input file, assert that we don't
  // crash or report an error when parsing.
  for (off_t size = statbuf.st_size; size > 0; --size) {
#ifndef _WIN32
    ASSERT_EQ(0, truncate(kTestFilename, size));
#else
    int fh;
    fh = _sopen(kTestFilename, _O_RDWR | _O_CREAT, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    ASSERT_EQ(0, _chsize(fh, size));
    _close(fh);
#endif

    BuildLog log2;
    EXPECT_TRUE(log2.Load(kTestFilename, &err));
    ASSERT_EQ("", err);
  }
}

TEST_F(BuildLogTest, UpgradeV3) {
  FILE* f = fopen(kTestFilename, "wb");
  fprintf(f, "# ninja log v3\n");
  fprintf(f, "123 456 0 out command\n");
  fclose(f);

  string err;
  BuildLog log;
  EXPECT_TRUE(log.Load(kTestFilename, &err));
  ASSERT_EQ("", err);

  BuildLog::LogEntry* e = log.LookupByOutput("out");
  ASSERT_TRUE(e);
  ASSERT_EQ(123, e->start_time);
  ASSERT_EQ(456, e->end_time);
  ASSERT_EQ(0, e->restat_mtime);
  ASSERT_EQ("command", e->command);
}

TEST_F(BuildLogTest, SpacesInOutputV4) {
  FILE* f = fopen(kTestFilename, "wb");
  fprintf(f, "# ninja log v4\n");
  fprintf(f, "123\t456\t456\tout with space\tcommand\n");
  fclose(f);

  string err;
  BuildLog log;
  EXPECT_TRUE(log.Load(kTestFilename, &err));
  ASSERT_EQ("", err);

  BuildLog::LogEntry* e = log.LookupByOutput("out with space");
  ASSERT_TRUE(e);
  ASSERT_EQ(123, e->start_time);
  ASSERT_EQ(456, e->end_time);
  ASSERT_EQ(456, e->restat_mtime);
  ASSERT_EQ("command", e->command);
}
