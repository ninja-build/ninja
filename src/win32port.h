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

#ifndef NINJA_WIN32PORT_H_
#define NINJA_WIN32PORT_H_

#if defined(__MINGW32__) || defined(__MINGW64__)
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>
#endif

typedef signed short int16_t;
typedef unsigned short uint16_t;
/// A 64-bit integer type
typedef signed long long int64_t;
typedef unsigned long long uint64_t;

// printf format specifier for uint64_t, from C99.
#ifndef PRIu64
#define PRId64 "I64d"
#define PRIu64 "I64u"
#define PRIx64 "I64x"
#endif

#endif // NINJA_WIN32PORT_H_

