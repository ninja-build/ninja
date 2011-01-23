#include "ninja.h"

#include <getopt.h>
#include <limits.h>
#include <stdio.h>

#include "build.h"
#include "build_log.h"
#include "parsers.h"

#include "graphviz.h"

// Import browse.py as binary data.
asm(
".data\n"
"browse_data_begin:\n"
".incbin \"src/browse.py\"\n"
"browse_data_end:\n"
);
// Declare the symbols defined above.
extern const char browse_data_begin[];
extern const char browse_data_end[];

option options[] = {
  { "help", no_argument, NULL, 'h' },
  { }
};

void usage() {
  fprintf(stderr,
"usage: ninja [options] target\n"
"\n"
"options:\n"
"  -g       output graphviz dot file for targets and exit\n"
"  -i FILE  specify input build file [default=build.ninja]\n"
"  -n       dry run (don't run commands but pretend they succeeded)\n"
"  -v       show all command lines\n"
"  -q       show inputs/outputs of target (query mode)\n"
"  -b       browse dependency graph of target in a web browser\n"
          );
}

struct RealFileReader : public ManifestParser::FileReader {
  bool ReadFile(const string& path, string* content, string* err) {
    return ::ReadFile(path, content, err) == 0;
  }
};

int CmdGraph(State* state, int argc, char* argv[]) {
  GraphViz graph;
  graph.Start();
  for (int i = 0; i < argc; ++i)
    graph.AddTarget(state->GetNode(argv[i]));
  graph.Finish();
  return 0;
}

int CmdQuery(State* state, int argc, char* argv[]) {
  for (int i = 0; i < argc; ++i) {
    Node* node = state->GetNode(argv[i]);
    if (node) {
      printf("%s:\n", argv[i]);
      if (node->in_edge_) {
        printf("  input: %s\n", node->in_edge_->rule_->name_.c_str());
        for (vector<Node*>::iterator in = node->in_edge_->inputs_.begin();
             in != node->in_edge_->inputs_.end(); ++in) {
          printf("    %s\n", (*in)->file_->path_.c_str());
        }
      }
      for (vector<Edge*>::iterator edge = node->out_edges_.begin();
           edge != node->out_edges_.end(); ++edge) {
        printf("  output: %s\n", (*edge)->rule_->name_.c_str());
        for (vector<Node*>::iterator out = (*edge)->outputs_.begin();
             out != (*edge)->outputs_.end(); ++out) {
          printf("    %s\n", (*out)->file_->path_.c_str());
        }
      }
    } else {
      printf("%s unknown\n", argv[i]);
      return 1;
    }
  }
  return 0;
}

int CmdBrowse(State* state, int argc, char* argv[]) {
  // Create a temporary file, dump the Python code into it, and
  // delete the file, keeping our open handle to it.
  char tmpl[] = "browsepy-XXXXXX";
  int fd = mkstemp(tmpl);
  unlink(tmpl);
  const int browse_data_len = browse_data_end - browse_data_begin;
  int len = write(fd, browse_data_begin, browse_data_len);
  if (len < browse_data_len) {
    perror("write");
    return 1;
  }

  // exec Python, telling it to use our script file.
  const char* command[] = {
    "python", "/proc/self/fd/3", argv[0], NULL
  };
  execvp(command[0], (char**)command);

  // If we get here, the exec failed.
  printf("ERROR: Failed to spawn python for graph browsing, aborting.\n");
  return 1;
}

int main(int argc, char** argv) {
  BuildConfig config;
  const char* input_file = "build.ninja";
  bool graph = false;
  bool query = false;
  bool browse = false;

  int opt;
  while ((opt = getopt_long(argc, argv, "bghi:nvq", options, NULL)) != -1) {
    switch (opt) {
      case 'g':
        graph = true;
        break;
      case 'i':
        input_file = optarg;
        break;
      case 'n':
        config.dry_run = true;
        break;
      case 'v':
        config.verbosity = BuildConfig::VERBOSE;
        break;
      case 'q':
        query = true;
        break;
      case 'b':
        browse = true;
        break;
      case 'h':
      default:
        usage();
        return 1;
    }
  }
  if (optind >= argc) {
    fprintf(stderr, "expected target to build\n");
    usage();
    return 1;
  }
  argv += optind;
  argc -= optind;

  char cwd[PATH_MAX];
  if (!getcwd(cwd, sizeof(cwd))) {
    perror("getcwd");
    return 1;
  }

  State state;
  RealFileReader file_reader;
  ManifestParser parser(&state, &file_reader);
  string err;
  if (!parser.Load(input_file, &err)) {
    fprintf(stderr, "error loading '%s': %s\n", input_file, err.c_str());
    return 1;
  }

  if (graph)
    return CmdGraph(&state, argc, argv);
  if (query)
    return CmdQuery(&state, argc, argv);
  if (browse)
    return CmdBrowse(&state, argc, argv);

  BuildLog build_log;
  build_log.SetConfig(&config);
  state.build_log_ = &build_log;

  const char* kLogPath = ".ninja_log";
  if (!build_log.Load(kLogPath, &err)) {
    fprintf(stderr, "error loading build log: %s\n", err.c_str());
    return 1;
  }

  if (!build_log.OpenForWrite(kLogPath, &err)) {
    fprintf(stderr, "error opening build log: %s\n", err.c_str());
    return 1;
  }

  Builder builder(&state, config);
  for (int i = 0; i < argc; ++i) {
    if (!builder.AddTarget(argv[i], &err)) {
      if (!err.empty()) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
      } else {
        // Added a target that is already up-to-date; not really
        // an error.
      }
    }
  }

  bool success = builder.Build(&err);
  if (!err.empty()) {
    printf("build stopped: %s.\n", err.c_str());
  }

  return success ? 0 : 1;
}
