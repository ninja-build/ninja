#include <map>
#include <string>
using namespace std;

struct Edge;

// Store a log of every command ran for every build.
// It has a few uses:
// 1) historical command lines for output files, so we know
//    when we need to rebuild due to the command changing
// 2) historical timing information
// 3) maybe we can generate some sort of build overview output
//    from it
struct BuildLog {
  bool OpenForWrite(const string& path, string* err);
  void RecordCommand(Edge* edge, int time_ms);
  void Close();

  // Load the on-disk log.
  bool Load(const string& path, string* err);

  struct LogEntry {
    string output;
    string command;
    int time_ms;
    bool operator==(const LogEntry& o) {
      return output == o.output && command == o.command && time_ms == o.time_ms;
    }
  };

  // Lookup a previously-run command by its output path.
  LogEntry* LookupByOutput(const string& path);

  typedef map<string, LogEntry*> Log;
  Log log_;
  FILE* log_file_;
};
