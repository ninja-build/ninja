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

#pragma once

#include <stdarg.h>
#include <stdio.h>

#include <string>
#include <unordered_map>
#include <vector>

/// A class used to record a list of explanation strings associated
/// with a given 'item' pointer. This is used to implement the
/// `-d explain` feature.
struct Explanations {
 public:
  /// Record an explanation for |item| if this instance is enabled.
  void Record(const void* item, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    RecordArgs(item, fmt, args);
    va_end(args);
  }

  /// Same as Record(), but uses a va_list to pass formatting arguments.
  void RecordArgs(const void* item, const char* fmt, va_list args) {
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    map_[item].emplace_back(buffer);
  }

  /// Lookup the explanations recorded for |item|, and append them
  /// to |*out|, if any.
  void LookupAndAppend(const void* item, std::vector<std::string>* out) {
    auto it = map_.find(item);
    if (it == map_.end())
      return;

    for (const auto& explanation : it->second)
      out->push_back(explanation);
  }

 private:
  std::unordered_map<const void*, std::vector<std::string>> map_;
};

/// Convenience wrapper for an Explanations pointer, which can be null
/// if no explanations need to be recorded.
struct OptionalExplanations {
  OptionalExplanations(Explanations* explanations)
      : explanations_(explanations) {}

  void Record(const void* item, const char* fmt, ...) {
    if (explanations_) {
      va_list args;
      va_start(args, fmt);
      explanations_->RecordArgs(item, fmt, args);
      va_end(args);
    }
  }

  void RecordArgs(const void* item, const char* fmt, va_list args) {
    if (explanations_)
      explanations_->RecordArgs(item, fmt, args);
  }

  void LookupAndAppend(const void* item, std::vector<std::string>* out) {
    if (explanations_)
      explanations_->LookupAndAppend(item, out);
  }

  Explanations* ptr() const { return explanations_; }

 private:
  Explanations* explanations_;
};
