// Copyright 2018 Google Inc. All Rights Reserved.
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

#include "tokenpool.h"

#include "test.h"

#ifndef _WIN32
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define ENVIRONMENT_CLEAR() unsetenv("MAKEFLAGS")
#define ENVIRONMENT_INIT(v) setenv("MAKEFLAGS", v, true);
#endif

namespace {

const double kLoadAverageDefault = -1.23456789;

struct TokenPoolTest : public testing::Test {
  double load_avg_;
  TokenPool *tokens_;
#ifndef _WIN32
  char buf_[1024];
  int fds_[2];
#endif

  virtual void SetUp() {
    load_avg_ = kLoadAverageDefault;
    tokens_ = NULL;
#ifndef _WIN32
    ENVIRONMENT_CLEAR();
    if (pipe(fds_) < 0)
      ASSERT_TRUE(false);
#endif
  }

  void CreatePool(const char *format, bool ignore_jobserver) {
#ifndef _WIN32
    if (format) {
      sprintf(buf_, format, fds_[0], fds_[1]);
      ENVIRONMENT_INIT(buf_);
    }
#endif
    tokens_ = TokenPool::Get(ignore_jobserver, false, load_avg_);
  }

  void CreateDefaultPool() {
    CreatePool("foo --jobserver-auth=%d,%d bar", false);
  }

  virtual void TearDown() {
    if (tokens_)
      delete tokens_;
#ifndef _WIN32
    close(fds_[0]);
    close(fds_[1]);
    ENVIRONMENT_CLEAR();
#endif
  }
};

} // anonymous namespace

// verifies none implementation
TEST_F(TokenPoolTest, NoTokenPool) {
  CreatePool(NULL, false);

  EXPECT_EQ(NULL, tokens_);
  EXPECT_EQ(kLoadAverageDefault, load_avg_);
}

#ifndef _WIN32
TEST_F(TokenPoolTest, SuccessfulOldSetup) {
  // GNUmake <= 4.1
  CreatePool("foo --jobserver-fds=%d,%d bar", false);

  EXPECT_NE(NULL, tokens_);
  EXPECT_EQ(kLoadAverageDefault, load_avg_);
}

TEST_F(TokenPoolTest, SuccessfulNewSetup) {
  // GNUmake => 4.2
  CreateDefaultPool();

  EXPECT_NE(NULL, tokens_);
  EXPECT_EQ(kLoadAverageDefault, load_avg_);
}

TEST_F(TokenPoolTest, IgnoreWithJN) {
  CreatePool("foo --jobserver-auth=%d,%d bar", true);

  EXPECT_EQ(NULL, tokens_);
  EXPECT_EQ(kLoadAverageDefault, load_avg_);
}

TEST_F(TokenPoolTest, HonorLN) {
  CreatePool("foo -l9 --jobserver-auth=%d,%d bar", false);

  EXPECT_NE(NULL, tokens_);
  EXPECT_EQ(9.0, load_avg_);
}

TEST_F(TokenPoolTest, MonitorFD) {
  CreateDefaultPool();

  ASSERT_NE(NULL, tokens_);
  EXPECT_EQ(kLoadAverageDefault, load_avg_);

  EXPECT_EQ(fds_[0], tokens_->GetMonitorFd());
}

TEST_F(TokenPoolTest, ImplicitToken) {
  CreateDefaultPool();

  ASSERT_NE(NULL, tokens_);
  EXPECT_EQ(kLoadAverageDefault, load_avg_);

  EXPECT_TRUE(tokens_->Acquire());
  tokens_->Reserve();
  EXPECT_FALSE(tokens_->Acquire());
  tokens_->Release();
  EXPECT_TRUE(tokens_->Acquire());
}

TEST_F(TokenPoolTest, TwoTokens) {
  CreateDefaultPool();

  ASSERT_NE(NULL, tokens_);
  EXPECT_EQ(kLoadAverageDefault, load_avg_);

  // implicit token
  EXPECT_TRUE(tokens_->Acquire());
  tokens_->Reserve();
  EXPECT_FALSE(tokens_->Acquire());

  // jobserver offers 2nd token
  ASSERT_EQ(1u, write(fds_[1], "T", 1));
  EXPECT_TRUE(tokens_->Acquire());
  tokens_->Reserve();
  EXPECT_FALSE(tokens_->Acquire());

  // release 2nd token
  tokens_->Release();
  EXPECT_TRUE(tokens_->Acquire());

  // release implict token - must return 2nd token back to jobserver
  tokens_->Release();
  EXPECT_TRUE(tokens_->Acquire());

  // there must be one token in the pipe
  EXPECT_EQ(1u, read(fds_[0], buf_, sizeof(buf_)));

  // implicit token
  EXPECT_TRUE(tokens_->Acquire());
}

TEST_F(TokenPoolTest, Clear) {
  CreateDefaultPool();

  ASSERT_NE(NULL, tokens_);
  EXPECT_EQ(kLoadAverageDefault, load_avg_);

  // implicit token
  EXPECT_TRUE(tokens_->Acquire());
  tokens_->Reserve();
  EXPECT_FALSE(tokens_->Acquire());

  // jobserver offers 2nd & 3rd token
  ASSERT_EQ(2u, write(fds_[1], "TT", 2));
  EXPECT_TRUE(tokens_->Acquire());
  tokens_->Reserve();
  EXPECT_TRUE(tokens_->Acquire());
  tokens_->Reserve();
  EXPECT_FALSE(tokens_->Acquire());

  tokens_->Clear();
  EXPECT_TRUE(tokens_->Acquire());

  // there must be two tokens in the pipe
  EXPECT_EQ(2u, read(fds_[0], buf_, sizeof(buf_)));

  // implicit token
  EXPECT_TRUE(tokens_->Acquire());
}
#endif
