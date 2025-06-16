// Copyright 2025 Google Inc. All Rights Reserved.
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

#include "jobserver_pool.h"

#include "jobserver.h"
#include "test.h"

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

TEST(JobserverPoolTest, DefaultPool) {
  const size_t kSlotCount = 10;
  std::string error;
  auto pool = JobserverPool::Create(kSlotCount, &error);
  ASSERT_TRUE(pool.get()) << error;
  EXPECT_TRUE(error.empty());

  std::string makeflags = pool->GetEnvMakeFlagsValue();
#ifdef _WIN32
  std::string auth_prefix = " -j10 --jobserver-auth=";
#else   // !_WIN32
  std::string auth_prefix = " -j10 --jobserver-auth=fifo:";
#endif  // !_WIN32
  ASSERT_EQ(auth_prefix, makeflags.substr(0, auth_prefix.size()));

  // Parse the MAKEFLAGS value to create a JobServer::Config
  Jobserver::Config config;
  ASSERT_TRUE(
      Jobserver::ParseMakeFlagsValue(makeflags.c_str(), &config, &error));
  EXPECT_EQ(config.mode, Jobserver::Config::kModeDefault);

  // Create a client from the Config, and try to read all slots.
  std::unique_ptr<Jobserver::Client> client =
      Jobserver::Client::Create(config, &error);
  EXPECT_TRUE(client.get());
  EXPECT_TRUE(error.empty()) << error;

  // First slot is always implicit.
  Jobserver::Slot slot = client->TryAcquire();
  EXPECT_TRUE(slot.IsValid());
  EXPECT_TRUE(slot.IsImplicit());

  // Then read kSlotCount - 1 slots from the pipe.
  for (size_t n = 1; n < kSlotCount; ++n) {
    slot = client->TryAcquire();
    EXPECT_TRUE(slot.IsValid()) << "Slot #" << n + 1;
    EXPECT_TRUE(slot.IsExplicit()) << "Slot #" << n + 1;
  }

  // Pool should be empty now, so next TryAcquire() will fail.
  slot = client->TryAcquire();
  EXPECT_FALSE(slot.IsValid());
}
