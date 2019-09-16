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

#include <iostream>
#include <string>

namespace ninja {

class Logger {
public:
  enum Level {
    ERROR = 0,
    WARNING = 1
  };

  virtual void OnMessage(Level level, const std::string& message) = 0;
};

class LoggerBasic : Logger {
public:
  virtual void OnMessage(Level level, const std::string& message) {
    std::cerr << "level " << level << ": " << message << std::endl;
  }
};

class LoggerNull : Logger {
  virtual void OnMessage(Level level, const std::string& message) {
  }

};

}  // namespace ninja
#endif  // NINJA_PUBLIC_LOGGER_H_
