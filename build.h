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

private:
  void NodeFinished(Node* node);

  set<Edge*> want_;
  set<Edge*> ready_;
};

struct Subprocess {
  Subprocess();
  ~Subprocess();
  bool Start(const string& command, string* err);
  void OnFDReady(int fd);
  bool Finish(string* err);

  bool done() const {
    return stdout_.fd_ == -1 && stderr_.fd_ == -1;
  }

  struct Stream {
    Stream();
    ~Stream();
    int fd_;
    string buf_;
  };
  Stream stdout_, stderr_;
  pid_t pid_;
  string err_;
};

struct SubprocessSet {
  void Add(Subprocess* subprocess);
  void DoWork(string* err);

  int max_running_;
  vector<Subprocess*> running_;
  queue<Subprocess*> finished_;
};

struct CommandRunner {
  virtual ~CommandRunner() {}
  virtual bool StartCommand(Edge* edge) = 0;
  virtual void WaitForCommands(string* err) = 0;
  virtual Edge* NextFinishedCommand() = 0;
};

struct Builder {
  Builder(State* state);

  Node* AddTarget(const string& name, string* err);
  bool Build(string* err);

  bool StartEdge(Edge* edge, string* err);

  State* state_;
  Plan plan_;
  DiskInterface* disk_interface_;
  CommandRunner* command_runner_;
};

#endif  // NINJA_BUILD_H_
