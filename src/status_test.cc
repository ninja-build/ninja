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

#include "status.h"

#include "test.h"

TEST(StatusTest, StatusFormatElapsed) {
  BuildConfig config;
  StatusPrinter status(config);

  status.BuildStarted();
  // Before any task is done, the elapsed time must be zero.
  EXPECT_EQ("[%/e0.000]",
            status.FormatProgressStatus("[%%/e%e]", 0));
}

TEST(StatusTest, StatusFormatReplacePlaceholder) {
  BuildConfig config;
  StatusPrinter status(config);

  EXPECT_EQ("[%/s0/t0/r0/u0/f0]",
            status.FormatProgressStatus("[%%/s%s/t%t/r%r/u%u/f%f]", 0));
}
