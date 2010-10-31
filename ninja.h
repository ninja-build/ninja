#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include <assert.h>

using namespace std;

#include "eval_env.h"

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

struct Node;
struct FileStat {
  FileStat(const string& path) : path_(path), mtime_(-1), node_(NULL) {}
  void Touch(int mtime);
  // Return true if the file exists (mtime_ got a value).
  bool Stat(DiskInterface* disk_interface);

  // Return true if we needed to stat.
  bool StatIfNecessary(DiskInterface* disk_interface) {
    if (status_known())
      return false;
    Stat(disk_interface);
    return true;
  }

  bool exists() const {
    assert(status_known());
    return mtime_ != 0;
  }

  bool status_known() const {
    return mtime_ != -1;
  }

  string path_;
  // Possible values of mtime_:
  //   -1: file hasn't been examined
  //   0:  we looked, and file doesn't exist
  //   >0: actual file's mtime
  time_t mtime_;
  Node* node_;
};

struct Edge;
struct Node {
  Node(FileStat* file) : file_(file), dirty_(false), in_edge_(NULL) {}

  bool dirty() const { return dirty_; }
  void MarkDirty();
  void MarkDependentsDirty();

  FileStat* file_;
  bool dirty_;
  Edge* in_edge_;
  vector<Edge*> out_edges_;
};

struct Rule {
  Rule(const string& name) : name_(name) { }

  void ParseCommand(const string& command) {
    assert(command_.Parse(command));  // XXX
  }
  string name_;
  EvalString command_;
  EvalString depfile_;
};

class State;
struct Edge {
  Edge() : rule_(NULL), env_(NULL), implicit_deps_(0) {}

  void MarkDirty(Node* node);
  bool RecomputeDirty(State* state, DiskInterface* disk_interface, string* err);
  string EvaluateCommand();  // XXX move to env, take env ptr
  bool LoadDepFile(State* state, DiskInterface* disk_interface, string* err);

  enum InOut { IN, OUT };

  Rule* rule_;
  vector<Node*> inputs_;
  vector<Node*> outputs_;
  EvalString::Env* env_;
  int implicit_deps_;  // Count on the end of the inputs list.
};

struct StatCache {
  typedef map<string, FileStat*> Paths;
  Paths paths_;
  FileStat* GetFile(const string& path);
  void Dump();
  void Reload();
};
struct State : public EvalString::Env {
  StatCache stat_cache_;
  map<string, Rule*> rules_;
  vector<Edge*> edges_;
  map<string, string> env_;

  StatCache* stat_cache() { return &stat_cache_; }

  // EvalString::Env impl
  virtual string Evaluate(const string& var);

  void AddRule(Rule* rule);
  Rule* LookupRule(const string& rule_name);
  Edge* AddEdge(Rule* rule);
  Node* GetNode(const string& path);
  Node* LookupNode(const string& path);
  void AddInOut(Edge* edge, Edge::InOut inout, const string& path);
  void AddBinding(const string& key, const string& val);
};

struct Plan {
  explicit Plan(State* state) : state_(state) {}

  Node* AddTarget(const string& path, string* err);
  bool AddTarget(Node* node, string* err);

  Edge* FindWork();
  void EdgeFinished(Edge* edge);
  void NodeFinished(Node* node);

  State* state_;
  set<Node*> want_;
  queue<Edge*> ready_;

private:
  Plan();
  Plan(const Plan&);
};


struct Shell {
  virtual ~Shell() {}
  virtual bool RunCommand(Edge* edge);
};

struct Builder {
  Builder(State* state)
      : state_(state), plan_(state), disk_interface_(&default_disk_interface_) {}
  virtual ~Builder() {}

  Node* AddTarget(const string& name, string* err);
  bool Build(Shell* shell, string* err);

  State* state_;
  Plan plan_;
  RealDiskInterface default_disk_interface_;
  DiskInterface* disk_interface_;
};
