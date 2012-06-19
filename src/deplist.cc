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

#include "deplist.h"

#ifndef _MSC_VER
#include <stdint.h>
#else
#include <windows.h>
#include "util.h"
#endif
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "dep_database.h"
#include "metrics.h"
#include "string_piece.h"

namespace {

const int kVersion = 1;

/// Read a 16-bit native-byte-order integer from *in and advance *in past
/// it.
int16_t ReadInt16(const char** in) {
  // Use memcpy to read out values to avoid type-punning problems;
  // memcpy is blessed by the standard and compiles down to the single
  // load/store you'd expect.
  int16_t out;
  memcpy(&out, *in, 2);
  *in += 2;
  return out;
}

/// Write a 16-bit native-byte-order integer to *out and advance *out past it.
void WriteUint16(char **out, uint16_t value) {
  memcpy(*out, &value, 2);
  *out += 2;
}

}  // anonymous namespace

// static
bool Deplist::Write(FILE* file, const vector<StringPiece>& entries) {
  int16_t version = kVersion;
  uint16_t count = static_cast<uint16_t>(entries.size());
  if (fwrite(&version, 2, 1, file) < 1)
    return false;
   if (fwrite(&count, 2, 1, file) < 1)
    return false;

  for (vector<StringPiece>::const_iterator i = entries.begin();
       i != entries.end(); ++i) {
    int16_t length = static_cast<int16_t>(i->len_);
    if (fwrite(&length, 2, 1, file) < 1)
      return false;
  }

  for (vector<StringPiece>::const_iterator i = entries.begin();
       i != entries.end(); ++i) {
    if (fwrite(i->str_, i->len_, 1, file) < 1)
      return false;
  }
  return true;
}

#ifdef _WIN32
// static
const char *Deplist::WriteDatabase(DepDatabase& depdb,
                                   const string& filename,
                                   const vector<StringPiece>& entries) {
  // Serialize to memory.
  size_t data_size = 2;
  data_size += entries.size() * 2;
  for (vector<StringPiece>::const_iterator i = entries.begin();
       i != entries.end(); ++i)
    data_size += i->len_;

  char* data = new char[data_size];
  char* out = data;
  WriteUint16(&out, static_cast<uint16_t>(entries.size()));
  for (vector<StringPiece>::const_iterator i = entries.begin();
       i != entries.end(); ++i) {
    WriteUint16(&out, static_cast<uint16_t>(i->len_));
  }
  for (vector<StringPiece>::const_iterator i = entries.begin();
       i != entries.end(); ++i) {
    memcpy(out, i->str_, i->len_);
    out += i->len_;
  }

  depdb.InsertOrUpdateDepData(filename, data, data_size);
  delete[] data;
  return 0;
}
//
// static
bool Deplist::LoadNoHeader(
    StringPiece input, vector<StringPiece>* entries, string* err) {
  const char* in = input.str_;
  int16_t count = ReadInt16(&in);
  const char* strings = in + (count * 2);
  entries->resize(count);
  for (int i = 0; i < count; ++i) {
    int16_t len = ReadInt16(&in);
    (*entries)[i] = StringPiece(strings, len);
    strings += len;
  }
  return true;
}
#endif

// static
bool Deplist::Load(StringPiece input, vector<StringPiece>* entries,
                   string* err) {
  const char* in = input.str_;
  const char* end = input.str_ + input.len_;

  if (end - in < 2 * 2) {
    *err = "unexpected EOF";
    return false;
  }

  int16_t version = ReadInt16(&in);
  if (version != kVersion) {
    *err = "version mismatch";
    return false;
  }
  int16_t count = ReadInt16(&in);

  if (end - in < count * 2) {
    *err = "unexpected EOF";
    return false;
  }
  const char* strings = in + (count * 2);

  entries->resize(count);
  for (int i = 0; i < count; ++i) {
    int16_t len = ReadInt16(&in);
    (*entries)[i] = StringPiece(strings, len);
    strings += len;
  }

  if (strings > end) {
    *err = "unexpected EOF";
    return false;
  }

  return true;
}
