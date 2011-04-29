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

#ifndef NINJA_CLEAN_H_
#define NINJA_CLEAN_H_

#include <string>
#include <set>
using namespace std;

struct State;
struct BuildConfig;
struct Node;
struct Rule;

class Cleaner
{
public:
  /// Constructor.
  Cleaner(State* state, const BuildConfig& config);

  /// Clean the given @a target and all the file built for it.
  void CleanTarget(Node* target);
  int CleanTargets(int target_count, char* targets[]);

  /// Clean all built files.
  void CleanAll();

  /// Clean all the file built with the given rule @a rule.
  void CleanRule(const Rule* rule);
  int CleanRules(int rule_count, char* rules[]);

private:
  /// Remove the file @a path.
  /// @return whether the file has been removed.
  bool RemoveFile(const string& path);
  /// @returns whether the file @a path exists.
  bool FileExists(const string& path);
  void Report(const string& path);
  /// Remove the given @a path file only if it has not been already removed.
  void Remove(const string& path);
  /// @return whether the given @a path has already been removed.
  bool IsAlreadyRemoved(const string& path);
  /// Helper recursive method for CleanTarget().
  void DoCleanTarget(Node* target);
  void PrintHeader();
  void PrintFooter();
  void DoCleanRule(const Rule* rule);

private:
  State* state_;
  bool verbose_;
  bool dry_run_;
  set<string> removed_;
};

#endif  // NINJA_CLEAN_H_
