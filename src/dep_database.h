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

#include <windows.h>
#include <string>
using namespace std;

struct StringPiece;
struct DbData;

// DepDatabase is persistent faster storage for the equivalent of .d files.
// See the .cc for a description of the format and operation.
struct DepDatabase {
  // Create or open the DepDatabase with the given filename. If create is
  // true, will create the given file, if necessary.
  DepDatabase(const string& filename, bool create);

  ~DepDatabase();

  // Find the dependency information for a given file, or null if not
  // contained in the database.
  const char* FindDepData(const string& filename);

  // Add dependency information for the given filename, or replace the old
  // data if the path was already in the database.
  void InsertOrUpdateDepData(const string& filename, const char* data, int size);

  void DumpIndex();
  void DumpDeps(const string& filename);

private:
  // Acquire exclusive access.
  void Acquire();

  // Release exclusive access.
  void Release();

  DbData* GetView() const;
  char* GetDataAt(int offset) const;
  
  void IncreaseFileSize();
  void SetEmptyData();

  // Global mutex for entire structure.
  HANDLE lock_;

  HANDLE file_;
  HANDLE file_mapping_;
  void* view_;
  int size_;
};
