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

#ifndef NINJA_TOUCH_H_
#define NINJA_TOUCH_H_

#include <string>
#include <set>
using namespace std;

struct State;
struct BuildConfig;
struct Node;
struct Rule;

class Toucher
{
public:
  /// Constructor.
  Toucher(State* state, const BuildConfig& config);

  /// Touch the given @a target and all the file built for it.
  void TouchTarget(Node* target);
  int TouchTarget(const char* target);
  int TouchTargets(int target_count, const char* targets[]);

  /// Touch all built files.
  void TouchAll();

  /// Touch all the file built with the given rule @a rule.
  void TouchRule(const Rule* rule);
  int TouchRule(const char* rule);
  int TouchRules(int rule_count, const char* rules[]);


private:
  /// Touch the file @a path.
  /// @return whether the file has been removed.
  bool TouchFile(const string& path);
  /// @returns whether the file @a path exists.
  bool FileExists(const string& path);
  void Report(const string& path);
  /// Touch the given @a path file only if it has not been already touched.
  void Touch(const string& path);
  /// @return whether the given @a path has already been touched.
  bool IsAlreadyTouched(const string& path);
  /// Helper recursive method for TouchTarget().
  void DoTouchTarget(Node* target);
  void PrintHeader();
  void PrintFooter();
  void DoTouchRule(const Rule* rule);

private:
  State* state_;
  bool verbose_;
  bool dry_run_;
  set<string> touched_;
};

#endif  // NINJA_TOUCH_H_
