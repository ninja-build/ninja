// Copyright 2016 Google Inc. All Rights Reserved.
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

#ifndef NINJA_FILEBUF_H_
#define NINJA_FILEBUF_H_

#include <stdio.h>

#include <streambuf>

// A non-buffering std::streambuf implementation that allows using
// a FILE* as an ostream.
class ofilebuf : public std::streambuf {
 public:
  ofilebuf(FILE* f) : f_(f) { }
  ~ofilebuf() { }

 private:
  int_type overflow(int_type c) {
    if (c != traits_type::eof()) {
      int ret = fputc(c, f_);
      if (ret == EOF) {
        return traits_type::eof();
      }
    }

    return c;
  }

  std::streamsize xsputn(const char* s, std::streamsize count) {
    return fwrite(s, 1, count, f_);
  }

 private:
  FILE* f_;
};

#endif // NINJA_FILEBUF_H_
