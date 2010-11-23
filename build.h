#include <set>
#include <string>
using namespace std;

struct Edge;
struct DiskInterface;
struct Node;
struct State;

// Plan stores the state of a build plan: what we intend to build,
// which steps we're ready to execute.
struct Plan {
  // Add a target to our plan (including all its dependencies).
  // Returns false if we don't need to build this target; may
  // fill in |err| with an error message if there's a problem.
  bool AddTarget(Node* node, string* err);

  // Pop a ready edge off the queue of edges to build.
  // Returns NULL if there's no work to do.
  Edge* FindWork();

  // Returns true if there's more work to be done.
  bool more_to_do() const { return !want_.empty(); }

  // Dumps the current state of the plan.
  void Dump();

  // Mark an edge as done building.  Used internally and by
  // tests.
  void EdgeFinished(Edge* edge);

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
