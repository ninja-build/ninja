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

#include <stdint.h>
#include <string>
#include <vector>
#include <fuzzer/FuzzedDataProvider.h>

#include "util.h"
#include "edit_distance.h"
#include "json.h"
#include "string_piece_util.h"
#include "clparser.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider fdp(data, size);

  // Fuzz CLParser
  {
    std::string deps_prefix = fdp.ConsumeRandomLengthString(32);
    std::string output = fdp.ConsumeRandomLengthString(256);
    CLParser parser;
    std::string filtered_output, err;
    parser.Parse(output, deps_prefix, &filtered_output, &err);
  }

  // Fuzz EditDistance
  std::string s1 = fdp.ConsumeRandomLengthString(100);
  std::string s2 = fdp.ConsumeRandomLengthString(100);
  bool allow_replacements = fdp.ConsumeBool();
  int max_edit_distance = fdp.ConsumeIntegralInRange<int>(0, 10);
  EditDistance(s1, s2, allow_replacements, max_edit_distance);

  // Fuzz CanonicalizePath
  std::string path = fdp.ConsumeRandomLengthString(256);
  uint64_t slash_bits = 0;
  CanonicalizePath(&path, &slash_bits);

  // Fuzz Shell Escaping
  std::string unescaped = fdp.ConsumeRandomLengthString(256);
  std::string escaped;
  GetShellEscapedString(unescaped, &escaped);
  escaped.clear();
  GetWin32EscapedString(unescaped, &escaped);

  // Fuzz StripAnsiEscapeCodes
  std::string ansi_in = fdp.ConsumeRandomLengthString(256);
  StripAnsiEscapeCodes(ansi_in);

  // Fuzz SpellcheckStringV
  {
    std::string text = fdp.ConsumeRandomLengthString(20);
    int num_words = fdp.ConsumeIntegralInRange<int>(0, 10);
    std::vector<std::string> words_s;
    for (int i = 0; i < num_words; ++i) {
      words_s.push_back(fdp.ConsumeRandomLengthString(20));
    }
    std::vector<const char*> words_c;
    for (int i = 0; i < num_words; ++i) {
      words_c.push_back(words_s[i].c_str());
    }
    SpellcheckStringV(text, words_c);
  }

  // Fuzz EncodeJSONString
  std::string json_in = fdp.ConsumeRandomLengthString(256);
  EncodeJSONString(json_in);

  // Fuzz StringPieceUtil
  std::string sp_in = fdp.ConsumeRandomLengthString(256);
  char sep = fdp.ConsumeIntegral<char>();
  std::vector<StringPiece> pieces = SplitStringPiece(sp_in, sep);
  JoinStringPiece(pieces, sep);

  std::string sp_a = fdp.ConsumeRandomLengthString(100);
  std::string sp_b = fdp.ConsumeRandomLengthString(100);
  EqualsCaseInsensitiveASCII(sp_a, sp_b);

  return 0;
}
