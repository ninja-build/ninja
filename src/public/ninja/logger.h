// Copyright 2019 Google Inc. All Rights Reserved.
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
#ifndef NINJA_PUBLIC_LOGGER_H_
#define NINJA_PUBLIC_LOGGER_H_

#include <ostream>
#include <string>
#include <utility>

namespace ninja {

class Logger {
public:
  enum Level {
    ERROR = 0,
    WARNING = 1,
    INFO = 2
  };

  virtual void OnMessage(Level level, const std::string& message);

  inline void Error(const std::string& message) {
    OnMessage(ERROR, message);
  }
  inline void Warning(const std::string& message) {
    OnMessage(WARNING, message);
  }
  inline void Info(const std::string& message) {
    OnMessage(INFO, message);
  }

  virtual std::ostream& cout() = 0;
  virtual std::ostream& cerr() = 0;
};

class LoggerBasic : public Logger {
public:
  virtual std::ostream& cerr() override;
  virtual std::ostream& cout() override;
};

class NullBuffer : public std::streambuf
{
public:
  int overflow(int c) { return c; }
};

class LoggerNull : public Logger {
public:
  LoggerNull();
  ~LoggerNull();

  virtual std::ostream& cerr() override;
  virtual std::ostream& cout() override;
private:
  NullBuffer* null_buffer;
  std::ostream null_stream;
};


}  // namespace ninja
#endif  // NINJA_PUBLIC_LOGGER_H_
