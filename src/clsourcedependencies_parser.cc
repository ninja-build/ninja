// Copyright 2025 Google Inc. All Rights Reserved.
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

#include "clsourcedependencies_parser.h"

#include <cinttypes>
#include <string>
#include <utility>

#include "clparser.h"
#include "metrics.h"

// Include RapidJSON last because it may define conflicting inttypes macros.
// clang-format off
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
// clang-format on

bool ParseCLSourceDependencies(StringPiece content,
                               std::vector<std::string>* includes,
                               std::string* err) {
  METRIC_RECORD("ParseCLSourceDependencies");

  rapidjson::Document doc;
  {
    rapidjson::ParseResult result = doc.Parse(content.str_);
    if (result.IsError()) {
      *err = std::string("sourceDependencies is not valid JSON: ") +
             rapidjson::GetParseError_En(result.Code());
      return false;
    }
    if (!doc.IsObject()) {
      *err = "sourceDependencies is not an object";
      return false;
    }
  }

  {
    auto version_member = doc.FindMember("Version");
    if (version_member == doc.MemberEnd()) {
      *err = "sourceDependencies is missing Version";
      return false;
    }
    rapidjson::Value const& version = version_member->value;
    if (!version.IsString()) {
      *err = "sourceDependencies Version is not a string";
      return false;
    }
    char const* version_str = version.GetString();
    if (strncmp(version_str, "1.", 2) != 0) {
      *err = std::string("sourceDependencies Version is ") + version_str +
             ", but expected 1.x";
      return false;
    }
  }

  auto data_member = doc.FindMember("Data");
  if (data_member == doc.MemberEnd()) {
    *err = "sourceDependencies is missing Data";
    return false;
  }

  rapidjson::Value const& data = data_member->value;
  if (!data.IsObject()) {
    *err = "sourceDependencies Data is not an object";
    return false;
  }

  auto data_includes_member = data.FindMember("Includes");
  if (data_includes_member == data.MemberEnd()) {
    *err = "sourceDependencies Data is missing Includes";
    return false;
  }

  rapidjson::Value const& data_includes = data_includes_member->value;
  if (!data_includes.IsArray()) {
    *err = "sourceDependencies Data/Includes is not an array";
    return false;
  }

  for (auto i = data_includes.Begin(); i != data_includes.End(); ++i) {
    if (!i->IsString()) {
      *err = "sourceDependencies Data/Includes element is not a string";
      return false;
    }

    std::string include_str = i->GetString();
    if (!CLParser::IsSystemInclude(include_str))
      includes->emplace_back(std::move(include_str));
  }

  return true;
}
