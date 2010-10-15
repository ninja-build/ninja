#include <map>
#include <string>
#include <vector>

using namespace std;

struct Node;
struct FileStat {
  FileStat(const string& path) : path_(path), node_(NULL) {}
  string path_;
  Node* node_;
};

struct Edge;
struct Node {
  Node(FileStat* file) : file_(file), dirty_(false) {}
  FileStat* file_;
  bool dirty_;
  vector<Edge*> edges_;
};

struct Rule {
  Rule(const string& name, const string& command) :
    name_(name), command_(command) {}
  string name_;
  string command_;
};

struct Edge {
  Rule* rule_;
  enum InOut { IN, OUT };
  vector<Node*> inputs_;
  vector<Node*> outputs_;
};

struct StatCache {
  map<string, FileStat*> paths_;
};
struct State {
  StatCache stat_cache_;
  map<string, Rule*> rules_;
  vector<Edge*> edges_;

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
  FileStat* file = new FileStat(path);
  stat_cache_.paths_[path] = file;
  return new Node(file);
}

void State::AddInOut(Edge* edge, Edge::InOut inout, const string& path) {
  Node* node = GetNode(path);
  if (inout == Edge::IN)
    edge->inputs_.push_back(node);
  else
    edge->outputs_.push_back(node);
}
