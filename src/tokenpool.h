// Copyright 2016 Google Inc. All Rights Reserved.
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

// interface to token pool
struct TokenPool {
  virtual ~TokenPool() {}

  virtual bool Acquire() = 0;
  virtual void Reserve() = 0;
  virtual void Release() = 0;
  virtual void Clear() = 0;

#ifdef _WIN32
  // @TODO
#else
  virtual int GetMonitorFd() = 0;
#endif

  // returns NULL if token pool is not available
  static struct TokenPool *Get(void);
};
