#include <string>
#include <vector>
#include <queue>
using namespace std;

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
