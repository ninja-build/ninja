#include <string>
#include <vector>
#include <queue>
using namespace std;

// Subprocess wraps a single async subprocess.  It is entirely
// passive: it expects the caller to notify it when its fds are ready
// for reading, as well as call Finish() to reap the child once done()
// is true.
struct Subprocess {
  Subprocess();
  ~Subprocess();
  bool Start(const string& command);
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
};

// SubprocessSet runs a poll() loop around a set of Subprocesses.
// DoWork() waits for any state change in subprocesses; finished_
// is a queue of subprocesses as they finish.
struct SubprocessSet {
  void Add(Subprocess* subprocess);
  void DoWork(string* err);
  Subprocess* NextFinished();

  vector<Subprocess*> running_;
  queue<Subprocess*> finished_;
};
