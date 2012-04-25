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

#ifndef NINJA_DEP_DATABASE_H_
#define NINJA_DEP_DATABASE_H_

#include <windows.h>
#include <string>
#include <vector>
using namespace std;

struct StringPiece;
struct DbData;

#include "lockable_mapped_file.h"

// DepDatabase is persistent faster storage for the equivalent of .d files.
// See the .cc for a description of the format and operation.
struct DepDatabase {
  // Create or open the DepDatabase with the given filename. If create is
  // true, will create the given file, if necessary.
  DepDatabase(const string& filename, bool create);
  ~DepDatabase();

  // Find the dependency information for a given file, or null if not
  // contained in the database.
  void StartLookups();
  // deps points into moveable data; only valid until FinishLookups.
  bool FindDepData(const string& filename, vector<StringPiece>* deps, string* err);
  void FinishLookups();

  // Add dependency information for the given filename, or replace the old
  // data if the path was already in the database. Handles locking internally.
  void InsertOrUpdateDepData(const string& filename, const char* data, int size);

  void DumpIndex();
  void DumpDeps(const string& filename);

private:
  DbData* GetView() const;
  char* GetDataAt(int offset) const;

  void SetEmptyData();

  LockableMappedFile data_;
};

#endif  // NINJA_DEP_DATABASE_H_
