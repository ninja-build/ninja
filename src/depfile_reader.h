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

#ifndef NINJA_DEPFILE_READER_H_
#define NINJA_DEPFILE_READER_H_

#include <string>
#include <map>
#include "disk_interface.h"
#include "depfile_parser.h"

using namespace std;

// Holds a DepfileParser together with its associated data.
// In the case of grouped depfiles, uses internal cache to store
// depfiles which have been read, but not used yet
struct DepfileReader {
  DepfileReader() : contents_(NULL), parser_(NULL) {};
  ~DepfileReader();

  // Read a depfile from disk and parse it
  bool Read(const string& depfile_path, 
            const string& output_name,
            DiskInterface * disk_interface, 
            string* err);

  // Read/retrieve from cache a part of a grouped depfile
  // associated with the given output and parse it.
  bool ReadGroup(const string& depfile_path, 
                 const string& output_name,
                 DiskInterface * disk_interface, 
                 string* err);

  inline DepfileParser * Parser() {
    return parser_;
  }

private:
  typedef map<string, map<string, DepfileReader> > DepfileCache;
  static DepfileCache cache;  
  static bool loadIntoCache(DiskInterface* disk_interface, const string& depfile_path, string* err);
  bool Init(const string& contents, string* error);

  friend struct DepfileReaderTest; // needs access to cache

  string* contents_;
  DepfileParser* parser_;
};

#endif // NINJA_DEPFILE_READER_H_
