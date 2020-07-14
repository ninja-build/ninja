// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License")
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

#if NINJA_ENABLE_CL_SOURCE_DEPENDENCIES

#include "clsourcedependencies_parser.h"

#include "rapidjson/document.h"

#include "clparser.h"
#include "metrics.h"
#include "util.h"

bool ParseCLSourceDependencies(const StringPiece content, std::string* err,
                               std::set<std::string>& includes) {
  METRIC_RECORD("CLSourceDependenciesParser::Parse");

  rapidjson::Document doc;
  if (doc.Parse(content.str_).HasParseError()) {
    *err = std::string("sourceDependencies parse error");
    return false;
  }

  if (!doc.IsObject()) {
    *err = std::string("sourceDependencies root must be an object");
    return false;
  }

  if (!doc.HasMember("Version")) {
    *err = std::string("sourceDependencies missing version");
    return false;
  }

  const rapidjson::Value& version = doc["Version"];
  if (!version.IsString()) {
    *err = std::string("sourceDependencies version must be a string");
    return false;
  }

  const char* version_str = version.GetString();
  if (strncmp(version_str, "1.", 2)) {
    *err = std::string("expected sourceDependencies version 1.x but found ") +
           version_str;
    return false;
  }

  if (!doc.HasMember("Data")) {
    *err = std::string("sourceDependencies missing data");
    return false;
  }

  const rapidjson::Value& data = doc["Data"];
  if (!data.IsObject()) {
    *err = std::string("sourceDependencies data must be an object");
    return false;
  }

  if (!data.HasMember("Includes")) {
    *err = std::string("sourceDependencies data missing includes");
    return false;
  }

  const rapidjson::Value& includes_val = data["Includes"];
  if (!includes_val.IsArray()) {
    *err = std::string("sourceDependencies includes must be an array");
    return false;
  }

  for (rapidjson::Value::ConstValueIterator i = includes_val.Begin();
       i != includes_val.End(); ++i) {
    if (!i->IsString()) {
      *err = std::string("sourceDependencies include path must be a string");
      return false;
    }

    std::string include_str = std::string(i->GetString());
    if (!CLParser::IsSystemInclude(include_str))
      includes.insert(include_str);
  }

  return true;
}

#endif  // NINJA_ENABLE_CL_SOURCE_DEPENDENCIES
