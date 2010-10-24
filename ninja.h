#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include <assert.h>

using namespace std;

#include "eval_env.h"

struct DiskInterface {
  // stat() a file, returning the mtime, or 0 if missing and -1 on other errors.
  virtual int Stat(const string& path);
  // Create a directory, returning false on failure.
  virtual bool MakeDir(const string& path);

  // Create all the parent directories for path; like mkdir -p `basename path`.
  bool MakeDirs(const string& path);
};

#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

int DiskInterface::Stat(const string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) < 0) {
    if (errno == ENOENT) {
      return 0;
    } else {
      fprintf(stderr, "stat(%s): %s\n", path.c_str(), strerror(errno));
      return -1;
    }
  }

  return st.st_mtime;
  return true;
}

string DirName(const string& path) {
  string::size_type slash_pos = path.rfind('/');
  if (slash_pos == string::npos)
    return "";  // Nothing to do.
  return path.substr(0, slash_pos);
}

bool DiskInterface::MakeDirs(const string& path) {
  string dir = DirName(path);
  if (dir.empty())
    return true;  // Reached root; assume it's there.
  int mtime = Stat(dir);
  if (mtime < 0)
    return false;  // Error.
  if (mtime > 0)
    return true;  // Exists already; we're done.

  // Directory doesn't exist.  Try creating its parent first.
  bool success = MakeDirs(dir);
  if (!success)
    return false;
  return MakeDir(dir);
}

bool DiskInterface::MakeDir(const string& path) {
  if (mkdir(path.c_str(), 0777) < 0) {
    fprintf(stderr, "mkdir(%s): %s\n", path.c_str(), strerror(errno));
    return false;
  }
  return true;
}

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
  Rule(const string& name, const string& command) :
    name_(name) {
    assert(command_.Parse(command));  // XXX
  }
  string name_;
  EvalString command_;
};

struct Edge {
  Edge() : rule_(NULL), env_(NULL) {}

  void MarkDirty(Node* node);
  void RecomputeDirty(DiskInterface* disk_interface);
  string EvaluateCommand();  // XXX move to env, take env ptr

  Rule* rule_;
  enum InOut { IN, OUT };
  vector<Node*> inputs_;
  vector<Node*> outputs_;
  EvalString::Env* env_;
};

void FileStat::Touch(int mtime) {
  mtime_ = mtime;
  if (node_)
    node_->MarkDirty();
}

bool FileStat::Stat(DiskInterface* disk_interface) {
  mtime_ = disk_interface->Stat(path_);
  return mtime_ > 0;
}

void Node::MarkDirty() {
  if (dirty_)
    return;  // We already know.

  dirty_ = true;
  MarkDependentsDirty();
}

void Node::MarkDependentsDirty() {
  for (vector<Edge*>::iterator i = out_edges_.begin(); i != out_edges_.end(); ++i)
    (*i)->MarkDirty(this);
}

void Edge::RecomputeDirty(DiskInterface* disk_interface) {
  bool dirty = false;

  time_t most_recent_input = 1;
  for (vector<Node*>::iterator i = inputs_.begin(); i != inputs_.end(); ++i) {
    if ((*i)->file_->StatIfNecessary(disk_interface)) {
      if (Edge* edge = (*i)->in_edge_)
        edge->RecomputeDirty(disk_interface);
      else
        (*i)->dirty_ = !(*i)->file_->exists();
    }
    if ((*i)->dirty_)
      dirty = true;
    else if ((*i)->file_->mtime_ > most_recent_input)
      most_recent_input = (*i)->file_->mtime_;
  }

  assert(!outputs_.empty());
  for (vector<Node*>::iterator i = outputs_.begin(); i != outputs_.end(); ++i) {
    assert((*i)->file_->status_known());
    if (dirty || (*i)->file_->mtime_ < most_recent_input) {
      (*i)->dirty_ = true;
    }
  }
}

void Edge::MarkDirty(Node* node) {
  vector<Node*>::iterator i = find(inputs_.begin(), inputs_.end(), node);
  if (i == inputs_.end())
    return;
  for (i = outputs_.begin(); i != outputs_.end(); ++i)
    (*i)->MarkDirty();
}

struct EdgeEnv : public EvalString::Env {
  EdgeEnv(Edge* edge) : edge_(edge) {}
  virtual string Evaluate(const string& var) {
    string result;
    if (var == "@in") {
      for (vector<Node*>::iterator i = edge_->inputs_.begin();
           i != edge_->inputs_.end(); ++i) {
        if (!result.empty())
          result.push_back(' ');
        result.append((*i)->file_->path_);
      }
    } else if (var == "$out") {
      result = edge_->outputs_[0]->file_->path_;
    } else if (edge_->env_) {
      return edge_->env_->Evaluate(var);
    }
    return result;
  }
  Edge* edge_;
};

string Edge::EvaluateCommand() {
  EdgeEnv env(this);
  return rule_->command_.Evaluate(&env);
}

struct StatCache {
  typedef map<string, FileStat*> Paths;
  Paths paths_;
  FileStat* GetFile(const string& path);
  void Dump();
  void Reload();
};

FileStat* StatCache::GetFile(const string& path) {
  Paths::iterator i = paths_.find(path);
  if (i != paths_.end())
    return i->second;
  FileStat* file = new FileStat(path);
  paths_[path] = file;
  return file;
}

#include <stdio.h>

void StatCache::Dump() {
  for (Paths::iterator i = paths_.begin(); i != paths_.end(); ++i) {
    FileStat* file = i->second;
    printf("%s %s\n",
           file->path_.c_str(),
           file->status_known()
           ? (file->node_->dirty_ ? "dirty" : "clean")
           : "unknown");
  }
}

struct State : public EvalString::Env {
  StatCache stat_cache_;
  map<string, Rule*> rules_;
  vector<Edge*> edges_;
  map<string, string> env_;

  StatCache* stat_cache() { return &stat_cache_; }

  // EvalString::Env impl
  virtual string Evaluate(const string& var);

  Rule* AddRule(const string& name, const string& command);
  Rule* LookupRule(const string& rule_name);
  Edge* AddEdge(Rule* rule);
  Node* GetNode(const string& path);
  Node* LookupNode(const string& path);
  void AddInOut(Edge* edge, Edge::InOut inout, const string& path);
  void AddBinding(const string& key, const string& val);
};

string State::Evaluate(const string& var) {
  if (var.size() > 1 && var[0] == '$') {
    map<string, string>::iterator i = env_.find(var.substr(1));
    if (i != env_.end())
      return i->second;
  }
  return "";
}


Rule* State::LookupRule(const string& rule_name) {
  map<string, Rule*>::iterator i = rules_.find(rule_name);
  if (i == rules_.end())
    return NULL;
  return i->second;
}

Rule* State::AddRule(const string& name, const string& command) {
  Rule* rule = new Rule(name, command);
  rules_[name] = rule;
  return rule;
}

Edge* State::AddEdge(Rule* rule) {
  Edge* edge = new Edge();
  edge->rule_ = rule;
  edge->env_ = this;
  edges_.push_back(edge);
  return edge;
}

Node* State::LookupNode(const string& path) {
  FileStat* file = stat_cache_.GetFile(path);
  if (!file->node_)
    return NULL;
  return file->node_;
}

Node* State::GetNode(const string& path) {
  FileStat* file = stat_cache_.GetFile(path);
  if (!file->node_)
    file->node_ = new Node(file);
  return file->node_;
}

void State::AddInOut(Edge* edge, Edge::InOut inout, const string& path) {
  Node* node = GetNode(path);
  if (inout == Edge::IN) {
    edge->inputs_.push_back(node);
    node->out_edges_.push_back(edge);
  } else {
    edge->outputs_.push_back(node);
    assert(node->in_edge_ == NULL);
    node->in_edge_ = edge;
  }
}

void State::AddBinding(const string& key, const string& val) {
  env_[key] = val;
}

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

Node* Plan::AddTarget(const string& path, string* err) {
  Node* node = state_->GetNode(path);
  AddTarget(node, err);
  return node;
}
bool Plan::AddTarget(Node* node, string* err) {
  Edge* edge = node->in_edge_;
  if (!edge) {  // Leaf node.
    if (node->dirty_) {
      *err = "'" + node->file_->path_ + "' missing and no known rule to make it";
      return false;
    }
    return false;
  }

  assert(edge);
  if (!node->dirty())
    return false;

  want_.insert(node);

  bool awaiting_inputs = false;
  for (vector<Node*>::iterator i = edge->inputs_.begin();
       i != edge->inputs_.end(); ++i) {
    if (AddTarget(*i, err))
      awaiting_inputs = true;
    else if (err && !err->empty())
      return false;
  }

  if (!awaiting_inputs)
    ready_.push(edge);

  return true;
}

Edge* Plan::FindWork() {
  if (ready_.empty())
    return NULL;
  Edge* edge = ready_.front();
  ready_.pop();
  return edge;
}

void Plan::EdgeFinished(Edge* edge) {
  // Check off any nodes we were waiting for with this edge.
  for (vector<Node*>::iterator i = edge->outputs_.begin();
       i != edge->outputs_.end(); ++i) {
    set<Node*>::iterator j = want_.find(*i);
    if (j != want_.end()) {
      NodeFinished(*j);
      want_.erase(j);
    }
  }
}

void Plan::NodeFinished(Node* node) {
  // See if we we want any edges from this node.
  for (vector<Edge*>::iterator i = node->out_edges_.begin();
       i != node->out_edges_.end(); ++i) {
    // See if we want any outputs from this edge.
    for (vector<Node*>::iterator j = (*i)->outputs_.begin();
         j != (*i)->outputs_.end(); ++j) {
      if (want_.find(*j) != want_.end()) {
        // See if the edge is ready.
        // XXX just track dirty counts.
        // XXX may double-enqueue edge.
        bool ready = true;
        for (vector<Node*>::iterator k = (*i)->inputs_.begin();
             k != (*i)->inputs_.end(); ++k) {
          if ((*k)->dirty()) {
            ready = false;
            break;
          }
        }
        if (ready)
          ready_.push(*i);
        break;
      }
    }
  }
}


#include "manifest_parser.h"

struct Shell {
  virtual ~Shell() {}
  virtual bool RunCommand(Edge* edge);
};

bool Shell::RunCommand(Edge* edge) {
  string err;
  string command = edge->EvaluateCommand();
  printf("  %s\n", command.c_str());
  int ret = system(command.c_str());
  if (WIFEXITED(ret)) {
    int exit = WEXITSTATUS(ret);
    if (exit == 0)
      return true;
    err = "nonzero exit status";
  } else {
    err = "something else went wrong";
  }
  return false;
}

struct Builder {
  Builder(State* state)
      : plan_(state), disk_interface_(&default_disk_interface_) {}
  virtual ~Builder() {}

  Node* AddTarget(const string& name, string* err) {
    Node* node = plan_.state_->LookupNode(name);
    if (!node) {
      *err = "unknown target: '" + name + "'";
      return NULL;
    }
    node->file_->StatIfNecessary(disk_interface_);
    if (node->in_edge_)
      node->in_edge_->RecomputeDirty(disk_interface_);
    if (!node->dirty_) {
      *err = "target is clean; nothing to do";
      return NULL;
    }
    if (!plan_.AddTarget(node, err))
      return NULL;
    return node;
  }
  bool Build(Shell* shell, string* err);

  Plan plan_;
  DiskInterface default_disk_interface_;
  DiskInterface* disk_interface_;
};

bool Builder::Build(Shell* shell, string* err) {
  if (plan_.want_.empty()) {
    *err = "no work to do";
    return true;
  }

  Edge* edge = plan_.FindWork();
  if (!edge) {
    *err = "unable to find work";
    return false;
  }

  do {
    // Create directories necessary for outputs.
    for (vector<Node*>::iterator i = edge->outputs_.begin();
         i != edge->outputs_.end(); ++i) {
      if (!disk_interface_->MakeDirs((*i)->file_->path_))
        return false;
    }

    string command = edge->EvaluateCommand();
    if (!shell->RunCommand(edge)) {
      err->assign("command '" + command + "' failed.");
      return false;
    }
    for (vector<Node*>::iterator i = edge->outputs_.begin();
         i != edge->outputs_.end(); ++i) {
      // XXX check that the output actually changed
      // XXX just notify node and have it propagate?
      (*i)->dirty_ = false;
    }
    plan_.EdgeFinished(edge);
  } while ((edge = plan_.FindWork()) != NULL);

  if (!plan_.want_.empty()) {
    *err = "ran out of work";
    return false;
  }

  return true;
}
