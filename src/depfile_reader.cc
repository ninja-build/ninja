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

#include <sstream>
#include "depfile_reader.h"

// A static cache for already parsed, but not yet used depfiles
DepfileReader::DepfileCache DepfileReader::cache;

DepfileReader::~DepfileReader() {
  delete contents_;
  delete parser_;
}

bool DepfileReader::Init(string& contents, string* error) {
  // init and take ownership of the string
  contents_ = new string();
  contents_->swap(contents);
  if (contents_->empty())    
    return true;

  parser_ = new DepfileParser();
  if (!parser_->Parse(contents_, error)) {
    return false;  
  }

  return true;
}

// Method to open, read and divide and aggregated depfile, saving its individual
// components into internal cache of DepFileReader
bool DepfileReader::loadIntoCache(DiskInterface* disk_interface, 
                                  const string& depfile_path, string* err)
{
  string contents = disk_interface->ReadFile(depfile_path, err);
  if (!err->empty())
    return false;

  // create an entry in the cache
  DepfileCache::mapped_type & fileMap = cache[depfile_path]; 
  
  // Populate it
  if (contents.empty())
    return true;
  istringstream stream(contents);
  string line, depfile_contents;
  while (getline(stream, line)) {
    depfile_contents.append(line).append("\n");
    if (line.empty() || line[line.length()-1] != '\\' || stream.eof()) {     
      // One depfile has ended here, let's parse it and put in cache
      DepfileReader reader;

      // Parse the depfile (to get the filename)
      // Note: depfile_contents gets cleared here
      string depfile_err;  
      if (!reader.Init(depfile_contents, &depfile_err)) {
        *err = depfile_path + ": " + depfile_err;  
        return false;  
      }
      
      // Save it in cache
      if (NULL != reader.parser_) {
        const string filename = reader.parser_->out().AsString();
        swap(fileMap[filename], reader);
      }   
    }
  }

  return true;
}

 bool DepfileReader::ReadGroup(const string& depfile_path, 
                               const string& output_name,
                               DiskInterface * disk_interface, 
                               string* err) {
  DepfileCache::iterator in_cache = cache.find(depfile_path);
  if (cache.end() == in_cache) {
    // file was not yet cached -> read it
    loadIntoCache(disk_interface, depfile_path, err);
    if (!err->empty()) {
      return false;
    }

    in_cache = cache.find(depfile_path);
  }

  // locate the relevant part of the cached file
  DepfileCache::mapped_type::iterator depfile_element = in_cache->second.find(output_name);
  if (in_cache->second.end() == depfile_element) {
    // no record for the underlying .d file -> not an error (it may be a new file)
    return true;
  }

  // retrieve the cached contents 
  swap(*this, depfile_element->second);

  // drop the element from the cache (it's only meant to be used once)
  in_cache->second.erase(depfile_element);

  return true;
}

bool DepfileReader::Read(const string& depfile_path, 
                         const string& output_name,
                         DiskInterface * disk_interface, 
                         string* err) {
  string contents = disk_interface->ReadFile(depfile_path, err);
  if (!err->empty())    
    return false;  

  if (contents.empty())
    return true;

  // Save and parse the file
  string depfile_err;
  if (!Init(contents, &depfile_err)) {
    *err = depfile_path + ": " + depfile_err;
    return false;
  }
      
  // Check that this depfile matches our output.  
  StringPiece opath = StringPiece(output_name);  
  if (opath != parser_->out()) {    
    *err = "expected depfile '" + depfile_path + "' to mention '" +
      output_name + "', got '" + parser_->out().AsString() + "'"; 
    return false;  
  }

  return true;
}
