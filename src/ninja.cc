// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/sysctl.h>
#elif defined(linux)
#include <sys/sysinfo.h>
#endif

#ifdef _WIN32
#include "getopt.h"
#include <direct.h>
#include <windows.h>
#else
#include <getopt.h>
#include <unistd.h>
#endif

#include "browse.h"
#include "build.h"
#include "build_log.h"
#include "clean.h"
#include "edit_distance.h"
#include "explain.h"
#include "graph.h"
#include "graphviz.h"
#include "manifest_parser.h"
#include "metrics.h"
#include "state.h"
#include "util.h"

namespace {

/// The version number of the current Ninja release.  This will always
/// be "git" on trunk.
const char* kVersion = "120715";

/// Global information passed into subtools.
struct Globals {
  Globals() : state(new State()) {}
  ~Globals() {
    delete state;
  }

  /// Deletes and recreates state so it is empty.
  void ResetState() {
    delete state;
    state = new State();
  }

  /// Command line used to run Ninja.
  const char* ninja_command;
  /// Build configuration (e.g. parallelism).
  BuildConfig config;
  /// Loaded state (rules, nodes). This is a pointer so it can be reset.
  State* state;
};

/// Print usage information.
void Usage(const BuildConfig& config) {
  fprintf(stderr,
"usage: ninja [options] [targets...]\n"
"\n"
"if targets are unspecified, builds the 'default' target (see manual).\n"
"targets are paths, with additional special syntax:\n"
"  'target^' means 'the first output that uses target'.\n"
"  example: 'ninja foo.cc^' will likely build foo.o.\n"
"\n"
"options:\n"
"  -C DIR   change to DIR before doing anything else\n"
"  -f FILE  specify input build file [default=build.ninja]\n"
"  -V       print ninja version (\"%s\")\n"
"\n"
"  -j N     run N jobs in parallel [default=%d]\n"
"  -l N     do not start new jobs if the load average is greater than N\n"
"  -k N     keep going until N jobs fail [default=1]\n"
"  -n       dry run (don't run commands but pretend they succeeded)\n"
"  -v       show all command lines while building\n"
"\n"
"  -d MODE  enable debugging (use -d list to list modes)\n"
"  -t TOOL  run a subtool\n"
"    use '-t list' to list subtools.\n"
"    terminates toplevel options; further flags are passed to the tool.\n",
          kVersion, config.parallelism);
}

/// Choose a default value for the -j (parallelism) flag.
int GuessParallelism() {
  int processors = 0;

#if defined(linux)
  processors = get_nprocs();
#elif defined(__APPLE__) || defined(__FreeBSD__)
  size_t processors_size = sizeof(processors);
  int name[] = {CTL_HW, HW_NCPU};
  if (sysctl(name, sizeof(name) / sizeof(int),
             &processors, &processors_size,
             NULL, 0) < 0) {
    processors = 1;
  }
#elif defined(_WIN32)
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  processors = info.dwNumberOfProcessors;
#endif

  switch (processors) {
  case 0:
  case 1:
    return 2;
  case 2:
    return 3;
  default:
    return processors + 2;
  }
}

/// An implementation of ManifestParser::FileReader that actually reads
/// the file.
struct RealFileReader : public ManifestParser::FileReader {
  bool ReadFile(const string& path, string* content, string* err) {
    return ::ReadFile(path, content, err) == 0;
  }
};

/// Rebuild the build manifest, if necessary.
/// Returns true if the manifest was rebuilt.
bool RebuildManifest(State* state, const BuildConfig& config,
                     const char* input_file, string* err) {
  string path = input_file;
  if (!CanonicalizePath(&path, err))
    return false;
  Node* node = state->LookupNode(path);
  if (!node)
    return false;

  Builder manifest_builder(state, config);
  if (!manifest_builder.AddTarget(node, err))
    return false;

  if (manifest_builder.AlreadyUpToDate())
    return false;  // Not an error, but we didn't rebuild.
  if (!manifest_builder.Build(err))
    return false;

  // The manifest was only rebuilt if it is now dirty (it may have been cleaned
  // by a restat).
  return node->dirty();
}

bool CollectTargetsFromArgs(State* state, int argc, char* argv[],
                            vector<Node*>* targets, string* err) {
  if (argc == 0) {
    *targets = state->DefaultNodes(err);
    if (!err->empty())
      return false;
  } else {
    for (int i = 0; i < argc; ++i) {
      string path = argv[i];
      if (!CanonicalizePath(&path, err))
        return false;

      // Special syntax: "foo.cc^" means "the first output of foo.cc".
      bool first_dependent = false;
      if (!path.empty() && path[path.size() - 1] == '^') {
        path.resize(path.size() - 1);
        first_dependent = true;
      }

      Node* node = state->LookupNode(path);
      if (node) {
        if (first_dependent) {
          if (node->out_edges().empty()) {
            *err = "'" + path + "' has no out edge";
            return false;
          }
          Edge* edge = node->out_edges()[0];
          if (edge->outputs_.empty()) {
            edge->Dump();
            Fatal("edge has no outputs");
          }
          node = edge->outputs_[0];
        }
        targets->push_back(node);
      } else {
        *err = "unknown target '" + path + "'";

        if (path == "clean") {
          *err += ", did you mean 'ninja -t clean'?";
        } else if (path == "help") {
          *err += ", did you mean 'ninja -h'?";
        } else {
          Node* suggestion = state->SpellcheckNode(path);
          if (suggestion) {
            *err += ", did you mean '" + suggestion->path() + "'?";
          }
        }
        return false;
      }
    }
  }
  return true;
}

int ToolGraph(Globals* globals, int argc, char* argv[]) {
  vector<Node*> nodes;
  string err;
  if (!CollectTargetsFromArgs(globals->state, argc, argv, &nodes, &err)) {
    Error("%s", err.c_str());
    return 1;
  }

  GraphViz graph;
  graph.Start();
  for (vector<Node*>::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
    graph.AddTarget(*n);
  graph.Finish();

  return 0;
}

int ToolQuery(Globals* globals, int argc, char* argv[]) {
  if (argc == 0) {
    Error("expected a target to query");
    return 1;
  }
  for (int i = 0; i < argc; ++i) {
    Node* node = globals->state->LookupNode(argv[i]);
    if (!node) {
      Node* suggestion = globals->state->SpellcheckNode(argv[i]);
      if (suggestion) {
        printf("%s unknown, did you mean %s?\n",
               argv[i], suggestion->path().c_str());
      } else {
        printf("%s unknown\n", argv[i]);
      }
      return 1;
    }

    printf("%s:\n", argv[i]);
    if (Edge* edge = node->in_edge()) {
      printf("  input: %s\n", edge->rule_->name().c_str());
      for (int in = 0; in < (int)edge->inputs_.size(); in++) {
        const char* label = "";
        if (edge->is_implicit(in))
          label = "| ";
        else if (edge->is_order_only(in))
          label = "|| ";
        printf("    %s%s\n", label, edge->inputs_[in]->path().c_str());
      }
    }
    printf("  outputs:\n");
    for (vector<Edge*>::const_iterator edge = node->out_edges().begin();
         edge != node->out_edges().end(); ++edge) {
      for (vector<Node*>::iterator out = (*edge)->outputs_.begin();
           out != (*edge)->outputs_.end(); ++out) {
        printf("    %s\n", (*out)->path().c_str());
      }
    }
  }
  return 0;
}

#if !defined(_WIN32) && !defined(NINJA_BOOTSTRAP)
int ToolBrowse(Globals* globals, int argc, char* argv[]) {
  if (argc < 1) {
    Error("expected a target to browse");
    return 1;
  }
  RunBrowsePython(globals->state, globals->ninja_command, argv[0]);
  // If we get here, the browse failed.
  return 1;
}
#endif  // _WIN32

int ToolTargetsList(const vector<Node*>& nodes, int depth, int indent) {
  for (vector<Node*>::const_iterator n = nodes.begin();
       n != nodes.end();
       ++n) {
    for (int i = 0; i < indent; ++i)
      printf("  ");
    const char* target = (*n)->path().c_str();
    if ((*n)->in_edge()) {
      printf("%s: %s\n", target, (*n)->in_edge()->rule_->name().c_str());
      if (depth > 1 || depth <= 0)
        ToolTargetsList((*n)->in_edge()->inputs_, depth - 1, indent + 1);
    } else {
      printf("%s\n", target);
    }
  }
  return 0;
}

int ToolTargetsSourceList(State* state) {
  for (vector<Edge*>::iterator e = state->edges_.begin();
       e != state->edges_.end(); ++e) {
    for (vector<Node*>::iterator inps = (*e)->inputs_.begin();
         inps != (*e)->inputs_.end(); ++inps) {
      if (!(*inps)->in_edge())
        printf("%s\n", (*inps)->path().c_str());
    }
  }
  return 0;
}

int ToolTargetsList(State* state, const string& rule_name) {
  set<string> rules;

  // Gather the outputs.
  for (vector<Edge*>::iterator e = state->edges_.begin();
       e != state->edges_.end(); ++e) {
    if ((*e)->rule_->name() == rule_name) {
      for (vector<Node*>::iterator out_node = (*e)->outputs_.begin();
           out_node != (*e)->outputs_.end(); ++out_node) {
        rules.insert((*out_node)->path());
      }
    }
  }

  // Print them.
  for (set<string>::const_iterator i = rules.begin();
       i != rules.end(); ++i) {
    printf("%s\n", (*i).c_str());
  }

  return 0;
}

int ToolTargetsList(State* state) {
  for (vector<Edge*>::iterator e = state->edges_.begin();
       e != state->edges_.end(); ++e) {
    for (vector<Node*>::iterator out_node = (*e)->outputs_.begin();
         out_node != (*e)->outputs_.end(); ++out_node) {
      printf("%s: %s\n",
             (*out_node)->path().c_str(),
             (*e)->rule_->name().c_str());
    }
  }
  return 0;
}

int ToolTargets(Globals* globals, int argc, char* argv[]) {
  int depth = 1;
  if (argc >= 1) {
    string mode = argv[0];
    if (mode == "rule") {
      string rule;
      if (argc > 1)
        rule = argv[1];
      if (rule.empty())
        return ToolTargetsSourceList(globals->state);
      else
        return ToolTargetsList(globals->state, rule);
    } else if (mode == "depth") {
      if (argc > 1)
        depth = atoi(argv[1]);
    } else if (mode == "all") {
      return ToolTargetsList(globals->state);
    } else {
      const char* suggestion =
          SpellcheckString(mode, "rule", "depth", "all", NULL);
      if (suggestion) {
        Error("unknown target tool mode '%s', did you mean '%s'?",
              mode.c_str(), suggestion);
      } else {
        Error("unknown target tool mode '%s'", mode.c_str());
      }
      return 1;
    }
  }

  string err;
  vector<Node*> root_nodes = globals->state->RootNodes(&err);
  if (err.empty()) {
    return ToolTargetsList(root_nodes, depth, 0);
  } else {
    Error("%s", err.c_str());
    return 1;
  }
}

int ToolRules(Globals* globals, int argc, char* /* argv */[]) {
  for (map<string, const Rule*>::iterator i = globals->state->rules_.begin();
       i != globals->state->rules_.end(); ++i) {
    if (i->second->description().empty()) {
      printf("%s\n", i->first.c_str());
    } else {
      printf("%s: %s\n",
             i->first.c_str(),
             // XXX I changed it such that we don't have an easy way
             // to get the source text anymore, so this output is
             // unsatisfactory.  How useful is this command, anyway?
             i->second->description().Serialize().c_str());
    }
  }
  return 0;
}

void PrintCommands(Edge* edge, set<Edge*>* seen) {
  if (!edge)
    return;
  if (!seen->insert(edge).second)
    return;

  for (vector<Node*>::iterator in = edge->inputs_.begin();
       in != edge->inputs_.end(); ++in)
    PrintCommands((*in)->in_edge(), seen);

  if (!edge->is_phony())
    puts(edge->EvaluateCommand().c_str());
}

int ToolCommands(Globals* globals, int argc, char* argv[]) {
  vector<Node*> nodes;
  string err;
  if (!CollectTargetsFromArgs(globals->state, argc, argv, &nodes, &err)) {
    Error("%s", err.c_str());
    return 1;
  }

  set<Edge*> seen;
  for (vector<Node*>::iterator in = nodes.begin(); in != nodes.end(); ++in)
    PrintCommands((*in)->in_edge(), &seen);

  return 0;
}

int ToolClean(Globals* globals, int argc, char* argv[]) {
  // The clean tool uses getopt, and expects argv[0] to contain the name of
  // the tool, i.e. "clean".
  argc++;
  argv--;

  bool generator = false;
  bool clean_rules = false;

  optind = 1;
  int opt;
  while ((opt = getopt(argc, argv, const_cast<char*>("hgr"))) != -1) {
    switch (opt) {
    case 'g':
      generator = true;
      break;
    case 'r':
      clean_rules = true;
      break;
    case 'h':
    default:
      printf("usage: ninja -t clean [options] [targets]\n"
"\n"
"options:\n"
"  -g     also clean files marked as ninja generator output\n"
"  -r     interpret targets as a list of rules to clean instead\n"
             );
    return 1;
    }
  }
  argv += optind;
  argc -= optind;

  if (clean_rules && argc == 0) {
    Error("expected a rule to clean");
    return 1;
  }

  Cleaner cleaner(globals->state, globals->config);
  if (argc >= 1) {
    if (clean_rules)
      return cleaner.CleanRules(argc, argv);
    else
      return cleaner.CleanTargets(argc, argv);
  } else {
    return cleaner.CleanAll(generator);
  }
}

void ToolUrtle() {
  // RLE encoded.
  const char* urtle =
" 13 ,3;2!2;\n8 ,;<11!;\n5 `'<10!(2`'2!\n11 ,6;, `\\. `\\9 .,c13$ec,.\n6 "
",2;11!>; `. ,;!2> .e8$2\".2 \"?7$e.\n <:<8!'` 2.3,.2` ,3!' ;,(?7\";2!2'<"
"; `?6$PF ,;,\n2 `'4!8;<!3'`2 3! ;,`'2`2'3!;4!`2.`!;2 3,2 .<!2'`).\n5 3`5"
"'2`9 `!2 `4!><3;5! J2$b,`!>;2!:2!`,d?b`!>\n26 `'-;,(<9!> $F3 )3.:!.2 d\""
"2 ) !>\n30 7`2'<3!- \"=-='5 .2 `2-=\",!>\n25 .ze9$er2 .,cd16$bc.'\n22 .e"
"14$,26$.\n21 z45$c .\n20 J50$c\n20 14$P\"`?34$b\n20 14$ dbc `2\"?22$?7$c"
"\n20 ?18$c.6 4\"8?4\" c8$P\n9 .2,.8 \"20$c.3 ._14 J9$\n .2,2c9$bec,.2 `?"
"21$c.3`4%,3%,3 c8$P\"\n22$c2 2\"?21$bc2,.2` .2,c7$P2\",cb\n23$b bc,.2\"2"
"?14$2F2\"5?2\",J5$P\" ,zd3$\n24$ ?$3?%3 `2\"2?12$bcucd3$P3\"2 2=7$\n23$P"
"\" ,3;<5!>2;,. `4\"6?2\"2 ,9;, `\"?2$\n";
  int count = 0;
  for (const char* p = urtle; *p; p++) {
    if ('0' <= *p && *p <= '9') {
      count = count*10 + *p - '0';
    } else {
      for (int i = 0; i < std::max(count, 1); ++i)
        printf("%c", *p);
      count = 0;
    }
  }
}

int RunTool(const string& tool, Globals* globals, int argc, char** argv) {
  typedef int (*ToolFunc)(Globals*, int, char**);
  struct Tool {
    const char* name;
    const char* desc;
    ToolFunc func;
  } tools[] = {
#if !defined(_WIN32) && !defined(NINJA_BOOTSTRAP)
    { "browse", "browse dependency graph in a web browser",
      ToolBrowse },
#endif
    { "clean", "clean built files",
      ToolClean },
    { "commands", "list all commands required to rebuild given targets",
      ToolCommands },
    { "graph", "output graphviz dot file for targets",
      ToolGraph },
    { "query", "show inputs/outputs for a path",
      ToolQuery },
    { "rules",    "list all rules",
      ToolRules },
    { "targets",  "list targets by their rule or depth in the DAG",
      ToolTargets },
    { NULL, NULL, NULL }
  };

  if (tool == "list") {
    printf("ninja subtools:\n");
    for (int i = 0; tools[i].name; ++i) {
      printf("%10s  %s\n", tools[i].name, tools[i].desc);
    }
    return 0;
  } else if (tool == "urtle") {
    ToolUrtle();
    return 0;
  }

  for (int i = 0; tools[i].name; ++i) {
    if (tool == tools[i].name)
      return tools[i].func(globals, argc, argv);
  }

  vector<const char*> words;
  for (int i = 0; tools[i].name; ++i)
    words.push_back(tools[i].name);
  const char* suggestion = SpellcheckStringV(tool, words);
  if (suggestion) {
    Error("unknown tool '%s', did you mean '%s'?", tool.c_str(), suggestion);
  } else {
    Error("unknown tool '%s'", tool.c_str());
  }
  return 1;
}

/// Enable a debugging mode.  Returns false if Ninja should exit instead
/// of continuing.
bool DebugEnable(const string& name, Globals* globals) {
  if (name == "list") {
    printf("debugging modes:\n"
"  stats    print operation counts/timing info\n"
"  explain  explain what caused a command to execute\n"
"multiple modes can be enabled via -d FOO -d BAR\n");
    return false;
  } else if (name == "stats") {
    g_metrics = new Metrics;
    return true;
  } else if (name == "explain") {
    g_explaining = true;
    return true;
  } else {
    printf("ninja: unknown debug setting '%s'\n", name.c_str());
    return false;
  }
}

int RunBuild(Globals* globals, int argc, char** argv) {
  string err;
  vector<Node*> targets;
  if (!CollectTargetsFromArgs(globals->state, argc, argv, &targets, &err)) {
    Error("%s", err.c_str());
    return 1;
  }

  Builder builder(globals->state, globals->config);
  for (size_t i = 0; i < targets.size(); ++i) {
    if (!builder.AddTarget(targets[i], &err)) {
      if (!err.empty()) {
        Error("%s", err.c_str());
        return 1;
      } else {
        // Added a target that is already up-to-date; not really
        // an error.
      }
    }
  }

  if (builder.AlreadyUpToDate()) {
    printf("ninja: no work to do.\n");
    return 0;
  }

  if (!builder.Build(&err)) {
    printf("ninja: build stopped: %s.\n", err.c_str());
    return 1;
  }

  return 0;
}

}  // anonymous namespace

int main(int argc, char** argv) {
  Globals globals;
  globals.ninja_command = argv[0];
  const char* input_file = "build.ninja";
  const char* working_dir = NULL;
  string tool;

  setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

  globals.config.parallelism = GuessParallelism();

  const option kLongOptions[] = {
    { "help", no_argument, NULL, 'h' },
    { NULL, 0, NULL, 0 }
  };

  int opt;
  while (tool.empty() &&
         (opt = getopt_long(argc, argv, "d:f:hj:k:l:nt:vC:V", kLongOptions,
                            NULL)) != -1) {
    switch (opt) {
      case 'd':
        if (!DebugEnable(optarg, &globals))
          return 1;
        break;
      case 'f':
        input_file = optarg;
        break;
      case 'j':
        globals.config.parallelism = atoi(optarg);
        break;
      case 'l': {
        char* end;
        double value = strtod(optarg, &end);
        if (end == optarg)
          Fatal("-l parameter not numeric: did you mean -l 0.0?");
        globals.config.max_load_average = value;
        break;
      }
      case 'k': {
        char* end;
        int value = strtol(optarg, &end, 10);
        if (*end != 0)
          Fatal("-k parameter not numeric; did you mean -k 0?");

        // We want to go until N jobs fail, which means we should allow
        // N failures and then stop.  For N <= 0, INT_MAX is close enough
        // to infinite for most sane builds.
        globals.config.failures_allowed = value > 0 ? value : INT_MAX;
        break;
      }
      case 'n':
        globals.config.dry_run = true;
        break;
      case 'v':
        globals.config.verbosity = BuildConfig::VERBOSE;
        break;
      case 't':
        tool = optarg;
        break;
      case 'C':
        working_dir = optarg;
        break;
      case 'V':
        printf("%s\n", kVersion);
        return 0;
      case 'h':
      default:
        Usage(globals.config);
        return 1;
    }
  }
  argv += optind;
  argc -= optind;

  if (working_dir) {
    // The formatting of this string, complete with funny quotes, is
    // so Emacs can properly identify that the cwd has changed for
    // subsequent commands.
    printf("ninja: Entering directory `%s'\n", working_dir);
#ifdef _WIN32
    if (_chdir(working_dir) < 0) {
#else
    if (chdir(working_dir) < 0) {
#endif
      Fatal("chdir to '%s' - %s", working_dir, strerror(errno));
    }
  }

  bool rebuilt_manifest = false;

reload:
  RealFileReader file_reader;
  ManifestParser parser(globals.state, &file_reader);
  string err;
  if (!parser.Load(input_file, &err)) {
    Error("%s", err.c_str());
    return 1;
  }

  if (!tool.empty())
    return RunTool(tool, &globals, argc, argv);

  BuildLog build_log;
  build_log.SetConfig(&globals.config);
  globals.state->build_log_ = &build_log;

  const string build_dir = globals.state->bindings_.LookupVariable("builddir");
  const char* kLogPath = ".ninja_log";
  string log_path = kLogPath;
  if (!build_dir.empty()) {
    if (MakeDir(build_dir) < 0 && errno != EEXIST) {
      Error("creating build directory %s: %s",
            build_dir.c_str(), strerror(errno));
      return 1;
    }
    log_path = build_dir + "/" + kLogPath;
  }

  if (!build_log.Load(log_path, &err)) {
    Error("loading build log %s: %s", log_path.c_str(), err.c_str());
    return 1;
  }

  if (!build_log.OpenForWrite(log_path, &err)) {
    Error("opening build log: %s", err.c_str());
    return 1;
  }

  if (!rebuilt_manifest) { // Don't get caught in an infinite loop by a rebuild
                           // target that is never up to date.
    if (RebuildManifest(globals.state, globals.config, input_file, &err)) {
      rebuilt_manifest = true;
      globals.ResetState();
      goto reload;
    } else if (!err.empty()) {
      Error("rebuilding '%s': %s", input_file, err.c_str());
      return 1;
    }
  }

  int result = RunBuild(&globals, argc, argv);
  if (g_metrics) {
    g_metrics->Report();

    printf("\n");
    int count = (int)globals.state->paths_.size();
    int buckets =
#ifdef _MSC_VER
        (int)globals.state->paths_.comp.bucket_size;
#else
        (int)globals.state->paths_.bucket_count();
#endif
    printf("path->node hash load %.2f (%d entries / %d buckets)\n",
           count / (double) buckets, count, buckets);
  }
  return result;
}
