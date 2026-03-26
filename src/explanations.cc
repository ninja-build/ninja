// Copyright 2026 Google Inc. All Rights Reserved.
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

#include "explanations.h"

#include <stdarg.h>
#include <stdio.h>

#include <string>
#include <unordered_map>
#include <vector>

void Explanations::Record(const void* item, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  RecordArgs(item, fmt, args);
  va_end(args);
}

void Explanations::RecordArgs(const void* item, const char* fmt, va_list args) {
  char buffer[1024];
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  map_[item].emplace_back(buffer);
}

void Explanations::LookupAndAppend(const void* item,
                                   std::vector<std::string>* out) {
  auto it = map_.find(item);
  if (it == map_.end()) {
    return;
  }

  for (const auto& explanation : it->second) {
    out->push_back(explanation);
  }
}

OptionalExplanations::OptionalExplanations(Explanations* explanations)
    : explanations_(explanations) {}

void OptionalExplanations::Record(const void* item, const char* fmt, ...) {
  if (explanations_) {
    va_list args;
    va_start(args, fmt);
    explanations_->RecordArgs(item, fmt, args);
    va_end(args);
  }
}

void OptionalExplanations::RecordArgs(const void* item, const char* fmt,
                                      va_list args) {
  if (explanations_) {
    explanations_->RecordArgs(item, fmt, args);
  }
}

void OptionalExplanations::LookupAndAppend(const void* item,
                                           std::vector<std::string>* out) {
  if (explanations_) {
    explanations_->LookupAndAppend(item, out);
  }
}
