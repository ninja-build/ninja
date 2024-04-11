// Copyright 2012 Google Inc. All Rights Reserved.
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

#ifndef INCLUDES_NORMALIZE_H_
#define INCLUDES_NORMALIZE_H_

#include <string>
#include <vector>

struct StringPiece;

/// Utility functions for normalizing include paths on Windows.
/// TODO: this likely duplicates functionality of CanonicalizePath; refactor.
struct IncludesNormalize {
  /// Normalize path relative to |relative_to|.
  IncludesNormalize(const std::string& relative_to);

  // Internal utilities made available for testing, maybe useful otherwise.
  static std::string AbsPath(StringPiece s, std::string* err);
  static std::string Relativize(StringPiece path,
                                const std::vector<StringPiece>& start_list,
                                std::string* err);

  /// Normalize by fixing slashes style, fixing redundant .. and . and makes the
  /// path |input| relative to |this->relative_to_| and store to |result|.
  bool Normalize(const std::string& input, std::string* result,
                 std::string* err) const;

 private:
  std::string relative_to_;
  std::vector<StringPiece> split_relative_to_;
};

#endif  // INCLUDES_NORMALIZE_H_
