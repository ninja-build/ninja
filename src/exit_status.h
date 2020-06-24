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

#ifndef NINJA_EXIT_STATUS_H_
#define NINJA_EXIT_STATUS_H_

enum ExitResult {
  ExitSuccess,
  ExitFailure,
  ExitInterrupted
};

struct ExitStatus {
  ExitStatus(): result(ExitSuccess), exit_code(0) {}
  ExitStatus(ExitResult result, int exit_code):
    result(result), exit_code(exit_code) {}
  ExitResult result;
  int exit_code;
};


#endif  // NINJA_EXIT_STATUS_H_
