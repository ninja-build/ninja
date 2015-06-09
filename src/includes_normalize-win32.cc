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

#include "includes_normalize.h"

#include "string_piece.h"
#include "util.h"

#include <algorithm>
#include <iterator>
#include <sstream>

#include <windows.h>

namespace {

/// Return true if paths a and b are on the same Windows drive.
bool SameDrive(StringPiece a, StringPiece b)  {
  char a_absolute[_MAX_PATH];
  char b_absolute[_MAX_PATH];
  GetFullPathName(a.AsString().c_str(), sizeof(a_absolute), a_absolute, NULL);
  GetFullPathName(b.AsString().c_str(), sizeof(b_absolute), b_absolute, NULL);
  char a_drive[_MAX_DIR];
  char b_drive[_MAX_DIR];
  _splitpath(a_absolute, a_drive, NULL, NULL, NULL);
  _splitpath(b_absolute, b_drive, NULL, NULL, NULL);
  return _stricmp(a_drive, b_drive) == 0;
}

}  // anonymous namespace

string IncludesNormalize::Join(const vector<string>& list, char sep) {
  string ret;
  for (size_t i = 0; i < list.size(); ++i) {
    ret += list[i];
    if (i != list.size() - 1)
      ret += sep;
  }
  return ret;
}

vector<string> IncludesNormalize::Split(const string& input, char sep) {
  vector<string> elems;
  stringstream ss(input);
  string item;
  while (getline(ss, item, sep))
    elems.push_back(item);
  return elems;
}

string IncludesNormalize::ToLower(const string& s) {
  string ret;
  transform(s.begin(), s.end(), back_inserter(ret), ::tolower);
  return ret;
}

string IncludesNormalize::AbsPath(StringPiece s) {
  char result[_MAX_PATH];
  GetFullPathName(s.AsString().c_str(), sizeof(result), result, NULL);
  for (char* c = result; *c; ++c)
    if (*c == '\\')
      *c = '/';
  return result;
}

string IncludesNormalize::Relativize(StringPiece path, const string& start) {
  vector<string> start_list = Split(AbsPath(start), '/');
  vector<string> path_list = Split(AbsPath(path), '/');
  int i;
  for (i = 0; i < static_cast<int>(min(start_list.size(), path_list.size()));
       ++i) {
    if (ToLower(start_list[i]) != ToLower(path_list[i]))
      break;
  }

  vector<string> rel_list;
  for (int j = 0; j < static_cast<int>(start_list.size() - i); ++j)
    rel_list.push_back("..");
  for (int j = i; j < static_cast<int>(path_list.size()); ++j)
    rel_list.push_back(path_list[j]);
  if (rel_list.size() == 0)
    return ".";
  return Join(rel_list, '/');
}

bool IncludesNormalize::Normalize(const string& input, const char* relative_to,
                                  string* result, string* err) {
  char copy[_MAX_PATH + 1];
  size_t len = input.size();
  if (len > _MAX_PATH) {
    *err = "path too long";
    return false;
  }
  strncpy(copy, input.c_str(), input.size() + 1);
  unsigned int slash_bits;
  if (!CanonicalizePath(copy, &len, &slash_bits, err))
    return false;
  StringPiece partially_fixed(copy, len);

  string curdir;
  if (!relative_to) {
    curdir = AbsPath(".");
    relative_to = curdir.c_str();
  }
  if (!SameDrive(partially_fixed, relative_to)) {
    *result = partially_fixed.AsString();
    return true;
  }
  *result = Relativize(partially_fixed, relative_to);
  return true;
}
