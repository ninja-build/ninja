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

#include <string>
using namespace std;

string EscapeForDepfile(const string& path);

/// Wraps a synchronous execution of a CL subprocess.
struct CLWrapper {
  CLWrapper() : env_block_(NULL) {}

  /// Set the environment block (as suitable for CreateProcess) to be used
  /// by Run().
  void SetEnvBlock(void* env_block) { env_block_ = env_block; }

  /// Start a process and gather its raw output.  Returns its exit code.
  /// Crashes (calls Fatal()) on error.
  int Run(const string& command, string* output);

  void* env_block_;
};
