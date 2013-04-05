// Copyright 2013 Google Inc. All Rights Reserved.
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

#ifndef NINJA_VERSION_H_
#define NINJA_VERSION_H_

#include <string>
using namespace std;

/// The version number of the current Ninja release.  This will always
/// be "git" on trunk.
extern const char* kNinjaVersion;

/// Parse the major/minor components of a version string.
void ParseVersion(const string& version, int* major, int* minor);

/// Check whether \a version is compatible with the current Ninja version,
/// aborting if not.
void CheckNinjaVersion(const string& required_version);

#endif  // NINJA_VERSION_H_
