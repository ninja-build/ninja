#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include <assert.h>

using namespace std;

struct Node;
struct FileStat {
  FileStat(const string& path) : path_(path), mtime_(0), node_(NULL) {}
  void Touch(int mtime);
  string path_;
  int mtime_;
  Node* node_;
};

struct Edge;
struct Node {
  Node(FileStat* file) : file_(file), dirty_(false), in_edge_(NULL) {}

  bool dirty() const { return dirty_; }
  void MarkDirty();

  FileStat* file_;
  bool dirty_;
  Edge* in_edge_;
  vector<Edge*> out_edges_;
};

struct Rule {
  Rule(const string& name, const string& command) :
    name_(name), command_(command) {}
  string name_;
  string command_;
};

struct Edge {
  Edge() : rule_(NULL) {}

  void MarkDirty(Node* node);
  string EvaluateCommand();  // XXX move to env, take env ptr

  Rule* rule_;
  enum InOut { IN, OUT };
  vector<Node*> inputs_;
  vector<Node*> outputs_;
};

void FileStat::Touch(int mtime) {
  if (node_)
    node_->MarkDirty();
}

void Node::MarkDirty() {
  if (dirty_)
    return;  // We already know.
  dirty_ = true;
  for (vector<Edge*>::iterator i = out_edges_.begin(); i != out_edges_.end(); ++i)
    (*i)->MarkDirty(this);
}

void Edge::MarkDirty(Node* node) {
  vector<Node*>::iterator i = find(inputs_.begin(), inputs_.end(), node);
  if (i == inputs_.end())
    return;
  for (i = outputs_.begin(); i != outputs_.end(); ++i)
    (*i)->MarkDirty();
}

string Edge::EvaluateCommand() {
  return rule_->command_;
}

struct StatCache {
  typedef map<string, FileStat*> Paths;
  Paths paths_;
  FileStat* GetFile(const string& path);
};

FileStat* StatCache::GetFile(const string& path) {
  Paths::iterator i = paths_.find(path);
  if (i != paths_.end())
    return i->second;
  FileStat* file = new FileStat(path);
  paths_[path] = file;
  return file;
}

struct State {
  StatCache stat_cache_;
  map<string, Rule*> rules_;
  vector<Edge*> edges_;

  StatCache* stat_cache() { return &stat_cache_; }

  Rule* AddRule(const string& name, const string& command);
  Edge* AddEdge(Rule* rule);
  Edge* AddEdge(const string& rule_name);
  Node* GetNode(const string& path);
  void AddInOut(Edge* edge, Edge::InOut inout, const string& path);
};

Rule* State::AddRule(const string& name, const string& command) {
  Rule* rule = new Rule(name, command);
  rules_[name] = rule;
  return rule;
}

Edge* State::AddEdge(const string& rule_name) {
  return AddEdge(rules_[rule_name]);
}

Edge* State::AddEdge(Rule* rule) {
  Edge* edge = new Edge();
  edge->rule_ = rule;
  edges_.push_back(edge);
  return edge;
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

struct Plan {
  Plan(State* state) : state_(state) {}

  void AddTarget(const string& path);
  bool AddTarget(Node* node);

  Edge* FindWork();

  State* state_;
  set<Node*> want_;
  queue<Edge*> ready_;
};

void Plan::AddTarget(const string& path) {
  AddTarget(state_->GetNode(path));
}
bool Plan::AddTarget(Node* node) {
  if (!node->dirty())
    return false;
  Edge* edge = node->in_edge_;
  if (!edge) {
    // TODO: if file doesn't exist we should die here.
    return false;
  }

  want_.insert(node);

  bool awaiting_inputs = false;
  for (vector<Node*>::iterator i = edge->inputs_.begin(); i != edge->inputs_.end(); ++i) {
    if (AddTarget(*i))
      awaiting_inputs = true;
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
