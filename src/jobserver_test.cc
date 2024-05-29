// Copyright 2024 Google Inc. All Rights Reserved.
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

#include "jobserver.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

/// Wrapper class to provide access to protected members of the Jobserver class.
struct JobserverWrapper : public Jobserver {
  /// Forwards calls to the protected Jobserver::ParseJobserverAuth() method.
  bool ParseJobserverAuth(const char* type) {
    return Jobserver::ParseJobserverAuth(type);
  }

  /// Provides access to the protected Jobserver::jobserver_name_ member.
  const char* GetJobserverName() const {
    return jobserver_name_.c_str();
  }
};

/// Jobserver state class that provides helpers to create, configure, and remove
/// "external" jobserver pools.
struct JobserverTest : public testing::Test {
  /// Save the initial MAKEFLAGS environment variable value to allow restoring
  /// it upon destruction.
  JobserverTest();

  /// Restores the MAKEFLAGS environment variable value recorded upon
  /// construction.
  ~JobserverTest();

  /// Configure the --jobserver-auth=<type>:<name> argument in the MAKEFLAGS
  /// environment value.
  void ServerConfigure(const char* name);

  /// Creates an external token pool with the given \a name and \a count number
  /// of tokens. Also configures the MAKEFLAGS environment variable with the
  /// correct --jobserver-auth argument to make the Jobserver class use the
  /// created external token pool.
  void ServerCreate(const char* name, size_t count);

  /// Return the number of tokens currently available in the external token
  /// pool.
  int ServerCount();

  /// Remove/close the handle to external token pool.
  void ServerRemove();

  /// Wrapped jobserver object to test on.
  JobserverWrapper jobserver_;

  /// Stored makeflags read before starting tests.
  const char* makeflags_ = nullptr;

  /// Name of created external jobserver token pool.
  const char* name_ = nullptr;

#ifdef _WIN32
  /// Implementation of posix setenv() for windows that forwards calls to
  /// _putenv().
  int setenv(const char* name, const char* value, int _) {
    std::string envstring;

    // _putenv() requires a single <name>=<value> string as argument.
    envstring += name;
    envstring += '=';
    envstring += value;

    return _putenv(envstring.c_str());
  };

  /// Implementation of posix unsetenv() for windows that forwards calls to
  /// _putenv().
  int unsetenv(const char* name) {
    /// Call _putenv() with <name>="" to unset the env variable.
    return setenv(name, "", 0);
  }

  /// Handle of the semaphore used as external token pool.
  HANDLE sem_ = INVALID_HANDLE_VALUE;
#else
  /// File descriptor of the fifo used as external token pool.
  int fd_ = -1;
#endif
};

JobserverTest::JobserverTest() {
  makeflags_ = getenv("MAKEFLAGS");
  unsetenv("MAKEFLAGS");
}

JobserverTest::~JobserverTest() {
  if (name_ != nullptr) {
    ServerRemove();
  }

  if (makeflags_ != nullptr) {
    setenv("MAKEFLAGS", makeflags_, 1);
  } else {
    unsetenv("MAKEFLAGS");
  }
}

void JobserverTest::ServerConfigure(const char* name)
{
  std::string makeflags("--jobserver-auth=");
#ifdef _WIN32
  makeflags += "sem:";
#else
  makeflags += "fifo:";
#endif
  makeflags += name;

  ASSERT_FALSE(setenv("MAKEFLAGS", makeflags.c_str(), 1)) << "failed to set make flags";
}

#ifdef _WIN32

void JobserverTest::ServerCreate(const char* name, size_t count) {
  ASSERT_EQ(name_, nullptr) << "external token pool server already created";
  ServerConfigure(name);
  name_ = name;

  // One cannot create a semaphore with a max value of 0 on windows
  sem_ = CreateSemaphoreA(nullptr, count, count ? count : 1, name);
  ASSERT_NE(sem_, nullptr) << "failed to create semaphore";
}

int JobserverTest::ServerCount() {
  if (name_ == nullptr) {
    return -1;
  }

  size_t count = 0;

  // First acquire all the available tokens to count them
  while (WaitForSingleObject(sem_, 0) == WAIT_OBJECT_0) {
    count++;
  }

  // Then return the acquired tokens to let the client continue
  ReleaseSemaphore(sem_, count, nullptr);

  return count;
}

void JobserverTest::ServerRemove() {
  ASSERT_NE(name_, nullptr) << "external token pool not created";
  CloseHandle(sem_);
  name_ = nullptr;
}

#else // _WIN32

void JobserverTest::ServerCreate(const char* name, size_t count) {
  ASSERT_EQ(name_, nullptr) << "external token pool already created";
  ServerConfigure(name);
  name_ = name;

  if (access(name, R_OK | W_OK)) {
    unlink(name);
  }

  // Create and open the fifo
  ASSERT_FALSE(mkfifo(name, S_IWUSR | S_IRUSR)) << "failed to create fifo";
  fd_ = open(name, O_RDWR | O_NONBLOCK);
  ASSERT_GE(fd_, 0) << "failed to open fifo";

  // Fill the fifo the requested number of tokens
  std::vector<char> tokens(count, '+');
  ASSERT_EQ(write(fd_, tokens.data(), count), count) << "failed to populate fifo";
}

int JobserverTest::ServerCount() {
  if (name_ == nullptr) {
    return -1;
  }

  size_t count = 0;
  char token;

  // First acquire all the available tokens to count them
  while (read(fd_, &token, 1) == 1) {
    count++;
  }

  // Then return the acquired tokens to let the client continue
  std::vector<char> tokens(count, '+');
  if (write(fd_, tokens.data(), tokens.size()) != tokens.size()) {
    return -1;
  }

  return count;
}

void JobserverTest::ServerRemove() {
  ASSERT_NE(name_, nullptr) << "external token pool not created";
  close(fd_);
  fd_ = -1;
  unlink(name_);
  name_ = nullptr;
}

#endif // _WIN32

TEST_F(JobserverTest, MakeFlags) {
  // Test with no make flags configured
  ASSERT_FALSE(unsetenv("MAKEFLAGS"));
  ASSERT_FALSE(jobserver_.ParseJobserverAuth("fifo"));
  ASSERT_FALSE(jobserver_.ParseJobserverAuth("sem"));

  // Test with no --jobserver-auth in make flags
  ASSERT_FALSE(setenv("MAKEFLAGS", "--other-arg=val", 0));
  ASSERT_FALSE(jobserver_.ParseJobserverAuth("fifo"));
  ASSERT_FALSE(jobserver_.ParseJobserverAuth("sem"));

  // Test fifo type
  ASSERT_FALSE(setenv("MAKEFLAGS", "--jobserver-auth=fifo:jobserver-1.fifo", 1));
  ASSERT_TRUE(jobserver_.ParseJobserverAuth("fifo"));
  ASSERT_STREQ(jobserver_.GetJobserverName(), "jobserver-1.fifo");

  // Test sem type
  ASSERT_FALSE(setenv("MAKEFLAGS", "--jobserver-auth=sem:jobserver-2-sem", 1));
  ASSERT_TRUE(jobserver_.ParseJobserverAuth("sem"));
  ASSERT_STREQ(jobserver_.GetJobserverName(), "jobserver-2-sem");

  // Test preceding arguments
  ASSERT_FALSE(setenv("MAKEFLAGS", "--other=val --jobserver-auth=fifo:jobserver-3.fifo", 1));
  ASSERT_TRUE(jobserver_.ParseJobserverAuth("fifo"));
  ASSERT_STREQ(jobserver_.GetJobserverName(), "jobserver-3.fifo");

  // Test following arguments
  ASSERT_FALSE(setenv("MAKEFLAGS", "--jobserver-auth=fifo:jobserver-4.fifo", 1));
  ASSERT_TRUE(jobserver_.ParseJobserverAuth("fifo"));
  ASSERT_STREQ(jobserver_.GetJobserverName(), "jobserver-4.fifo");

  // Test surrounding arguments
  ASSERT_FALSE(setenv("MAKEFLAGS", "--preceeding-arg=val --jobserver-auth=fifo:jobserver-5.fifo --following-arg=val", 1));
  ASSERT_TRUE(jobserver_.ParseJobserverAuth("fifo"));
  ASSERT_STREQ(jobserver_.GetJobserverName(), "jobserver-5.fifo");

  // Test invalid type
  ASSERT_FALSE(setenv("MAKEFLAGS", "--jobserver-auth=bad:jobserver-6", 1));
  ASSERT_FALSE(jobserver_.ParseJobserverAuth("fifo"));
  ASSERT_FALSE(jobserver_.ParseJobserverAuth("sem"));

  // Test missing type
  ASSERT_FALSE(setenv("MAKEFLAGS", "--jobserver-auth=", 1));
  ASSERT_FALSE(jobserver_.ParseJobserverAuth("fifo"));
  ASSERT_FALSE(jobserver_.ParseJobserverAuth("sem"));

  // Test missing colon
  ASSERT_FALSE(setenv("MAKEFLAGS", "--jobserver-auth=fifo", 1));
  ASSERT_FALSE(jobserver_.ParseJobserverAuth("fifo"));

  // Test missing colon following by another argument
  ASSERT_FALSE(setenv("MAKEFLAGS", "--jobserver-auth=fifo --other-arg=val", 1));
  ASSERT_FALSE(jobserver_.ParseJobserverAuth("fifo"));

  // Test missing colon following by another argument with a colon
  ASSERT_FALSE(setenv("MAKEFLAGS", "--jobserver-auth=fifo --other-arg=val0:val1", 1));
  ASSERT_FALSE(jobserver_.ParseJobserverAuth("fifo"));

  // Test missing value
  ASSERT_FALSE(setenv("MAKEFLAGS", "--jobserver-auth=fifo:", 1));
  ASSERT_FALSE(jobserver_.ParseJobserverAuth("fifo"));
}

TEST_F(JobserverTest, InitNoServer) {
  // Verify that the jobserver isn't enabled when no configuration is given
  jobserver_.Init();
  ASSERT_FALSE(jobserver_.Enabled());
}

TEST_F(JobserverTest, InitServer) {
  // Verify that the jobserver is enabled when a (valid) configuration is given
  ServerCreate("jobserver-init", 0);
  jobserver_.Init();
  ASSERT_TRUE(jobserver_.Enabled());
}

TEST_F(JobserverTest, InitFail) {
  // Verify that the jobserver exits with an error if a non-existing jobserver
  // is configured
  ServerConfigure("jobserver-missing");
  ASSERT_DEATH(jobserver_.Init(), "ninja: fatal: ");
}

TEST_F(JobserverTest, NoTokens) {
  // Verify that an empty token pool does in fact provide a "default" token
  ServerCreate("jobserver-empty", 0);

  jobserver_.Init();
  ASSERT_TRUE(jobserver_.Acquire());
  ASSERT_FALSE(jobserver_.Acquire());
  jobserver_.Release();
}

TEST_F(JobserverTest, OneToken) {
  // Verify that a token pool with exactly one token allows acquisition of one
  // "default" token and one "external" token
  ServerCreate("jobserver-one", 1);
  jobserver_.Init();

  for (int i = 0; i < 2; i++) {
    ASSERT_TRUE(jobserver_.Acquire());
  }

  ASSERT_FALSE(jobserver_.Acquire());

  for (int i = 0; i < 2; i++) {
    jobserver_.Release();
  }
}

TEST_F(JobserverTest, AcquireRelease) {
  // Verify that Acquire() takes a token from the external pool, and that
  // Release() returns it again.
  ServerCreate("jobserver-acquire-release", 5);
  jobserver_.Init();

  ASSERT_TRUE(jobserver_.Acquire());
  ASSERT_EQ(ServerCount(), 5);

  ASSERT_TRUE(jobserver_.Acquire());
  ASSERT_EQ(ServerCount(), 4);

  jobserver_.Release();
  ASSERT_EQ(ServerCount(), 5);

  jobserver_.Release();
  ASSERT_EQ(ServerCount(), 5);
}
