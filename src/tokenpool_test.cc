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

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
// should contain all valid characters
#define SEMAPHORE_NAME      "abcdefghijklmnopqrstwxyz01234567890_"
#define AUTH_FORMAT(tmpl)   "foo " tmpl "=%s bar"
#define ENVIRONMENT_CLEAR() SetEnvironmentVariable("MAKEFLAGS", NULL)
#define ENVIRONMENT_INIT(v) SetEnvironmentVariable("MAKEFLAGS", v)
#else
#define AUTH_FORMAT(tmpl)   "foo " tmpl "=%d,%d bar"
#define ENVIRONMENT_CLEAR() unsetenv("MAKEFLAGS")
#define ENVIRONMENT_INIT(v) setenv("MAKEFLAGS", v, true)
#endif

namespace {

const double kLoadAverageDefault = -1.23456789;

struct TokenPoolTest : public testing::Test {
  double load_avg_;
  TokenPool* tokens_;
  char buf_[1024];
#ifdef _WIN32
  const char* semaphore_name_;
  HANDLE semaphore_;
#else
  int fds_[2];
#endif

  virtual void SetUp() {
    load_avg_ = kLoadAverageDefault;
    tokens_ = NULL;
    ENVIRONMENT_CLEAR();
#ifdef _WIN32
    semaphore_name_ = SEMAPHORE_NAME;
    if ((semaphore_ = CreateSemaphore(0, 0, 2, SEMAPHORE_NAME)) == NULL)
#else
    if (pipe(fds_) < 0)
#endif
      ASSERT_TRUE(false);
  }

  void CreatePool(const char* format, bool ignore_jobserver = false) {
    if (format) {
      sprintf(buf_, format,
#ifdef _WIN32
              semaphore_name_
#else
              fds_[0], fds_[1]
#endif
      );
      ENVIRONMENT_INIT(buf_);
    }
    if ((tokens_ = TokenPool::Get()) != NULL) {
      if (!tokens_->Setup(ignore_jobserver, false, load_avg_)) {
        delete tokens_;
        tokens_ = NULL;
      }
    }
  }

  void CreateDefaultPool() {
    CreatePool(AUTH_FORMAT("--jobserver-auth"));
  }

  virtual void TearDown() {
    if (tokens_)
      delete tokens_;
#ifdef _WIN32
    CloseHandle(semaphore_);
#else
    close(fds_[0]);
    close(fds_[1]);
#endif
    ENVIRONMENT_CLEAR();
  }
};

} // anonymous namespace

// verifies none implementation
TEST_F(TokenPoolTest, NoTokenPool) {
  CreatePool(NULL, false);

  EXPECT_EQ(NULL, tokens_);
  EXPECT_EQ(kLoadAverageDefault, load_avg_);
}

TEST_F(TokenPoolTest, SuccessfulOldSetup) {
  // GNUmake <= 4.1
  CreatePool(AUTH_FORMAT("--jobserver-fds"));

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
  CreatePool(AUTH_FORMAT("--jobserver-auth"), true);

  EXPECT_EQ(NULL, tokens_);
  EXPECT_EQ(kLoadAverageDefault, load_avg_);
}

TEST_F(TokenPoolTest, HonorLN) {
  CreatePool(AUTH_FORMAT("-l9 --jobserver-auth"));

  EXPECT_NE(NULL, tokens_);
  EXPECT_EQ(9.0, load_avg_);
}

#ifdef _WIN32
TEST_F(TokenPoolTest, SemaphoreNotFound) {
  semaphore_name_ = SEMAPHORE_NAME "_foobar";
  CreateDefaultPool();

  EXPECT_EQ(NULL, tokens_);
  EXPECT_EQ(kLoadAverageDefault, load_avg_);
}

TEST_F(TokenPoolTest, TokenIsAvailable) {
  CreateDefaultPool();

  ASSERT_NE(NULL, tokens_);
  EXPECT_EQ(kLoadAverageDefault, load_avg_);

  EXPECT_TRUE(tokens_->TokenIsAvailable((ULONG_PTR)tokens_));
}
#else
TEST_F(TokenPoolTest, MonitorFD) {
  CreateDefaultPool();

  ASSERT_NE(NULL, tokens_);
  EXPECT_EQ(kLoadAverageDefault, load_avg_);

  EXPECT_EQ(fds_[0], tokens_->GetMonitorFd());
}
#endif

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
#ifdef _WIN32
  LONG previous;
  ASSERT_TRUE(ReleaseSemaphore(semaphore_, 1, &previous));
  ASSERT_EQ(0, previous);
#else
  ASSERT_EQ(1u, write(fds_[1], "T", 1));
#endif
  EXPECT_TRUE(tokens_->Acquire());
  tokens_->Reserve();
  EXPECT_FALSE(tokens_->Acquire());

  // release 2nd token
  tokens_->Release();
  EXPECT_TRUE(tokens_->Acquire());

  // release implict token - must return 2nd token back to jobserver
  tokens_->Release();
  EXPECT_TRUE(tokens_->Acquire());

  // there must be one token available
#ifdef _WIN32
  EXPECT_EQ(WAIT_OBJECT_0, WaitForSingleObject(semaphore_, 0));
  EXPECT_TRUE(ReleaseSemaphore(semaphore_, 1, &previous));
  EXPECT_EQ(0, previous);
#else
  EXPECT_EQ(1u, read(fds_[0], buf_, sizeof(buf_)));
#endif

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
#ifdef _WIN32
  LONG previous;
  ASSERT_TRUE(ReleaseSemaphore(semaphore_, 2, &previous));
  ASSERT_EQ(0, previous);
#else
  ASSERT_EQ(2u, write(fds_[1], "TT", 2));
#endif
  EXPECT_TRUE(tokens_->Acquire());
  tokens_->Reserve();
  EXPECT_TRUE(tokens_->Acquire());
  tokens_->Reserve();
  EXPECT_FALSE(tokens_->Acquire());

  tokens_->Clear();
  EXPECT_TRUE(tokens_->Acquire());

  // there must be two tokens available
#ifdef _WIN32
  EXPECT_EQ(WAIT_OBJECT_0, WaitForSingleObject(semaphore_, 0));
  EXPECT_EQ(WAIT_OBJECT_0, WaitForSingleObject(semaphore_, 0));
  EXPECT_TRUE(ReleaseSemaphore(semaphore_, 2, &previous));
  EXPECT_EQ(0, previous);
#else
  EXPECT_EQ(2u, read(fds_[0], buf_, sizeof(buf_)));
#endif

  // implicit token
  EXPECT_TRUE(tokens_->Acquire());
}
