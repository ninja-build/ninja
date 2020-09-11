// Copyright 2016-2018 Google Inc. All Rights Reserved.
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

// interface to GNU make token pool
struct GNUmakeTokenPool : public TokenPool {
  GNUmakeTokenPool();
  ~GNUmakeTokenPool();

  // token pool implementation
  virtual bool Acquire();
  virtual void Reserve();
  virtual void Release();
  virtual void Clear();
  virtual bool Setup(bool ignore, bool verbose, double& max_load_average);

  // platform specific implementation
  virtual const char* GetEnv(const char* name) = 0;
  virtual bool ParseAuth(const char* jobserver) = 0;
  virtual bool AcquireToken() = 0;
  virtual bool ReturnToken() = 0;

 private:
  int available_;
  int used_;

  void Return();
};
