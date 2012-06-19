// Copyright 2011 Google Inc. All Rights Reserved.
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

#include <string>
#include <vector>
using namespace std;

struct StringPiece;
struct DepDatabase;

/// Deplists are a fast serialization of lists of files, used for tracking
/// extra inputs quickly.  See the .cc file for a description of the format.
struct Deplist {

  /// Write out a list of strings to \a file.
  /// Returns false on error.
  static bool Write(FILE* file, const vector<StringPiece>& entries);

#ifdef _WIN32
  /// Write a list of strings to the DepDatabase.
  /// Returns error string on error, on NULL on success.
  static const char *WriteDatabase(DepDatabase& depdb,
                                   const string& filename,
                                   const vector<StringPiece>& entries);

  static bool LoadNoHeader(
      StringPiece input, vector<StringPiece>* entries, string* err);
#endif

  /// Parse a list of strings from \a input.  Returned entries are
  /// pointers within \a input.
  /// Returns false and fills in \a err on error.
  static bool Load(StringPiece input, vector<StringPiece>* entries,
                   string* err);
};
