#ifndef NINJA_NINJA_H_
#define NINJA_NINJA_H_

#include <algorithm>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include <assert.h>

using namespace std;

#include "eval_env.h"
#include "hash_map.h"

struct Edge;
struct FileStat;
struct Node;
struct Rule;

int ReadFile(const string& path, string* contents, string* err);

struct DiskInterface {
  // stat() a file, returning the mtime, or 0 if missing and -1 on other errors.
  virtual int Stat(const string& path) = 0;
  // Create a directory, returning false on failure.
  virtual bool MakeDir(const string& path) = 0;
  // Read a file to a string.  Fill in |err| on error.
  virtual string ReadFile(const string& path, string* err) = 0;

  // Create all the parent directories for path; like mkdir -p `basename path`.
  bool MakeDirs(const string& path);
};

struct RealDiskInterface : public DiskInterface {
  virtual int Stat(const string& path);
  virtual bool MakeDir(const string& path);
  virtual string ReadFile(const string& path, string* err);
};

struct StatCache {
  typedef hash_map<string, FileStat*> Paths;
  Paths paths_;
  FileStat* GetFile(const string& path);
  void Dump();
  void Reload();
};

struct State {
  State();

  StatCache* stat_cache() { return &stat_cache_; }

  void AddRule(const Rule* rule);
  const Rule* LookupRule(const string& rule_name);
  Edge* AddEdge(const Rule* rule);
  Node* GetNode(const string& path);
  Node* LookupNode(const string& path);
  void AddIn(Edge* edge, const string& path);
  void AddOut(Edge* edge, const string& path);

  StatCache stat_cache_;
  map<string, const Rule*> rules_;
  vector<Edge*> edges_;
  BindingEnv bindings_;
  struct BuildLog* build_log_;

  static const Rule kPhonyRule;
};

#endif  // NINJA_NINJA_H_
