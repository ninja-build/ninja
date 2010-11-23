#include <set>
#include <string>
using namespace std;

struct Edge;
struct DiskInterface;
struct Node;
struct State;

struct Plan {
  bool AddTarget(Node* node, string* err);

  Edge* FindWork();
  void EdgeFinished(Edge* edge);

  bool more_to_do() const { return !want_.empty(); }

  void Dump();

private:
  void NodeFinished(Node* node);

  set<Edge*> want_;
  set<Edge*> ready_;
};


struct Shell {
  virtual ~Shell() {}
  virtual bool RunCommand(Edge* edge);
};

struct Builder {
  Builder(State* state);

  Node* AddTarget(const string& name, string* err);
  bool Build(Shell* shell, string* err);

  State* state_;
  Plan plan_;
  DiskInterface* disk_interface_;
};
