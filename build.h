#ifndef NINJA_BUILD_H_
#define NINJA_BUILD_H_

#include <set>
#include <string>
#include <queue>
#include <vector>
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

  // Number of edges to run.
  int edge_count() const { return want_.size()  + ready_.size(); }

private:
  bool AddSubTarget(Node* node, vector<Node*>* stack, string* err);
  bool CheckDependencyCycle(Node* node, vector<Node*>* stack, string* err);
  void NodeFinished(Node* node);

  set<Edge*> want_;
  set<Edge*> ready_;
};

// CommandRunner is an interface that wraps running the build
// subcommands.  This allows tests to abstract out running commands.
// RealCommandRunner is an implementation that actually runs commands.
struct CommandRunner {
  virtual ~CommandRunner() {}
  virtual bool CanRunMore() = 0;
  virtual bool StartCommand(Edge* edge) = 0;
  virtual void WaitForCommands() = 0;
  virtual Edge* NextFinishedCommand(bool* success) = 0;
};

struct Builder {
  Builder(State* state);

  Node* AddTarget(const string& name, string* err);
  bool Build(string* err);

  bool StartEdge(Edge* edge, string* err);
  void FinishEdge(Edge* edge);

  State* state_;
  Plan plan_;
  DiskInterface* disk_interface_;
  CommandRunner* command_runner_;
};

#endif  // NINJA_BUILD_H_
