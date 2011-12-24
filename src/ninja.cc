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
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/sysctl.h>
#elif defined(linux)
#include <sys/sysinfo.h>
#endif

#ifdef WIN32
#include "getopt.h"
#include <direct.h>
#include <windows.h>
#else
#include <getopt.h>
#endif

#include "browse.h"
#include "build.h"
#include "build_log.h"
#include "clean.h"
#include "edit_distance.h"
#include "graph.h"
#include "graphviz.h"
#include "parsers.h"
#include "state.h"
#include "util.h"

namespace {

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
"\n"
"  -j N     run N jobs in parallel [default=%d]\n"
"  -k N     keep going until N jobs fail [default=1]\n"
"  -n       dry run (don't run commands but pretend they succeeded)\n"
"  -v       show all command lines while building\n"
"\n"
"  -t TOOL  run a subtool\n"
"    use '-t list' to list subtools.\n"
"    terminates toplevel options; further flags are passed to the tool.\n",
          config.parallelism);
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
#elif defined(WIN32)
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
  return manifest_builder.Build(err);
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

        Node* suggestion = state->SpellcheckNode(path);
        if (suggestion) {
          *err += ", did you mean '" + suggestion->path() + "'?";
        }
        return false;
      }
    }
  }
  return true;
}

int CmdGraph(State* state, int argc, char* argv[]) {
  vector<Node*> nodes;
  string err;
  if (!CollectTargetsFromArgs(state, argc, argv, &nodes, &err)) {
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

int CmdQuery(State* state, int argc, char* argv[]) {
  if (argc == 0) {
    Error("expected a target to query");
    return 1;
  }
  for (int i = 0; i < argc; ++i) {
    Node* node = state->LookupNode(argv[i]);
    if (node) {
      printf("%s:\n", argv[i]);
      if (node->in_edge()) {
        printf("  input: %s\n", node->in_edge()->rule_->name().c_str());
        for (vector<Node*>::iterator in = node->in_edge()->inputs_.begin();
             in != node->in_edge()->inputs_.end(); ++in) {
          printf("    %s\n", (*in)->path().c_str());
        }
      }
      for (vector<Edge*>::const_iterator edge = node->out_edges().begin();
           edge != node->out_edges().end(); ++edge) {
        printf("  output: %s\n", (*edge)->rule_->name().c_str());
        for (vector<Node*>::iterator out = (*edge)->outputs_.begin();
             out != (*edge)->outputs_.end(); ++out) {
          printf("    %s\n", (*out)->path().c_str());
        }
      }
    } else {
      Node* suggestion = state->SpellcheckNode(argv[i]);
      if (suggestion) {
        printf("%s unknown, did you mean %s?\n",
               argv[i], suggestion->path().c_str());
      } else {
        printf("%s unknown\n", argv[i]);
      }
      return 1;
    }
  }
  return 0;
}

int CmdBrowse(State* state, const char* ninja_command,
              int argc, char* argv[]) {
#ifndef WIN32
  if (argc < 1) {
    Error("expected a target to browse");
    return 1;
  }
  RunBrowsePython(state, ninja_command, argv[0]);
#else
  NINJA_UNUSED_ARG(state);
  NINJA_UNUSED_ARG(ninja_command);
  NINJA_UNUSED_ARG(argc);
  NINJA_UNUSED_ARG(argv);
  Error("browse mode not yet supported on Windows");
#endif
  // If we get here, the browse failed.
  return 1;
}

int CmdTargetsList(const vector<Node*>& nodes, int depth, int indent) {
  for (vector<Node*>::const_iterator n = nodes.begin();
       n != nodes.end();
       ++n) {
    for (int i = 0; i < indent; ++i)
      printf("  ");
    const char* target = (*n)->path().c_str();
    if ((*n)->in_edge()) {
      printf("%s: %s\n", target, (*n)->in_edge()->rule_->name().c_str());
      if (depth > 1 || depth <= 0)
        CmdTargetsList((*n)->in_edge()->inputs_, depth - 1, indent + 1);
    } else {
      printf("%s\n", target);
    }
  }
  return 0;
}

int CmdTargetsList(const vector<Node*>& nodes, int depth) {
  return CmdTargetsList(nodes, depth, 0);
}

int CmdTargetsSourceList(State* state) {
  for (vector<Edge*>::iterator e = state->edges_.begin();
       e != state->edges_.end();
       ++e)
    for (vector<Node*>::iterator inps = (*e)->inputs_.begin();
         inps != (*e)->inputs_.end();
         ++inps)
      if (!(*inps)->in_edge())
        printf("%s\n", (*inps)->path().c_str());
  return 0;
}

int CmdTargetsList(State* state, const string& rule_name) {
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

int CmdTargetsList(State* state) {
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

int CmdTargets(State* state, int argc, char* argv[]) {
  int depth = 1;
  if (argc >= 1) {
    string mode = argv[0];
    if (mode == "rule") {
      string rule;
      if (argc > 1)
        rule = argv[1];
      if (rule.empty())
        return CmdTargetsSourceList(state);
      else
        return CmdTargetsList(state, rule);
    } else if (mode == "depth") {
      if (argc > 1)
        depth = atoi(argv[1]);
    } else if (mode == "all") {
      return CmdTargetsList(state);
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
  vector<Node*> root_nodes = state->RootNodes(&err);
  if (err.empty()) {
    return CmdTargetsList(root_nodes, depth);
  } else {
    Error("%s", err.c_str());
    return 1;
  }
}

int CmdRules(State* state, int argc, char* /* argv */[]) {
  for (map<string, const Rule*>::iterator i = state->rules_.begin();
       i != state->rules_.end(); ++i) {
    if (i->second->description().unparsed_.empty()) {
      printf("%s\n", i->first.c_str());
    } else {
      printf("%s: %s\n",
             i->first.c_str(),
             i->second->description().unparsed_.c_str());
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

int CmdCommands(State* state, int argc, char* argv[]) {
  vector<Node*> nodes;
  string err;
  if (!CollectTargetsFromArgs(state, argc, argv, &nodes, &err)) {
    Error("%s", err.c_str());
    return 1;
  }

  set<Edge*> seen;
  for (vector<Node*>::iterator in = nodes.begin(); in != nodes.end(); ++in)
    PrintCommands((*in)->in_edge(), &seen);

  return 0;
}

int CmdClean(State* state, int argc, char* argv[], const BuildConfig& config) {
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

  Cleaner cleaner(state, config);
  if (argc >= 1) {
    if (clean_rules)
      return cleaner.CleanRules(argc, argv);
    else
      return cleaner.CleanTargets(argc, argv);
  } else {
    return cleaner.CleanAll(generator);
  }
}

/// Print out a list of tools.
int CmdList(State* state, int argc, char* argv[]) {
  printf("subtools:\n"
"             clean    clean built files\n"
"\n"
"             browse   browse dependency graph in a web browser\n"
"             graph    output graphviz dot file for targets\n"
"             query    show inputs/outputs for a path\n"
"\n"
"             targets  list targets by their rule or depth in the DAG\n"
"             rules    list all rules\n"
"             commands list all commands required to rebuild given targets\n"
         );
  return 0;
}

}  // anonymous namespace

int main(int argc, char** argv) {
  const char* ninja_command = argv[0];
  BuildConfig config;
  const char* input_file = "build.ninja";
  const char* working_dir = 0;
  string tool;

  setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

  config.parallelism = GuessParallelism();

  const option kLongOptions[] = {
    { "help", no_argument, NULL, 'h' },
    { NULL, 0, NULL, 0 }
  };

  int opt;
  while (tool.empty() &&
         (opt = getopt_long(argc, argv, "f:hj:k:nt:vC:", kLongOptions,
                            NULL)) != -1) {
    switch (opt) {
      case 'f':
        input_file = optarg;
        break;
      case 'j':
        config.parallelism = atoi(optarg);
        break;
      case 'k': {
        char* end;
        int value = strtol(optarg, &end, 10);
        if (*end != 0)
          Fatal("-k parameter not numeric; did you mean -k0?");

        // We want to go until N jobs fail, which means we should ignore
        // the first N-1 that fail and then stop.
        config.swallow_failures = value - 1;
        break;
      }
      case 'n':
        config.dry_run = true;
        break;
      case 'v':
        config.verbosity = BuildConfig::VERBOSE;
        break;
      case 't':
        tool = optarg;
        break;
      case 'C':
        working_dir = optarg;
        break;
      case 'h':
      default:
        Usage(config);
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
  State state;
  RealFileReader file_reader;
  ManifestParser parser(&state, &file_reader);
  string err;
  if (!parser.Load(input_file, &err)) {
    Error("loading '%s': %s", input_file, err.c_str());
    return 1;
  }

  if (!tool.empty()) {
    if (tool == "graph")
      return CmdGraph(&state, argc, argv);
    if (tool == "query")
      return CmdQuery(&state, argc, argv);
    if (tool == "browse")
      return CmdBrowse(&state, ninja_command, argc, argv);
    if (tool == "targets")
      return CmdTargets(&state, argc, argv);
    if (tool == "rules")
      return CmdRules(&state, argc, argv);
    if (tool == "commands")
      return CmdCommands(&state, argc, argv);
    // The clean tool uses getopt, and expects argv[0] to contain the name of
    // the tool, i.e. "clean".
    if (tool == "clean")
      return CmdClean(&state, argc+1, argv-1, config);
    if (tool == "list")
      return CmdList(&state, argc, argv);

    const char* suggestion = SpellcheckString(tool,
        "graph", "query", "browse", "targets", "rules", "commands", "clean",
        "list", NULL);
    if (suggestion) {
      Error("unknown tool '%s', did you mean '%s'?", tool.c_str(), suggestion);
    } else {
      Error("unknown tool '%s'", tool.c_str());
    }
    return 1;
  }

  BuildLog build_log;
  build_log.SetConfig(&config);
  state.build_log_ = &build_log;

  const string build_dir = state.bindings_.LookupVariable("builddir");
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

  if (!build_log.Load(log_path.c_str(), &err)) {
    Error("loading build log %s: %s",
          log_path.c_str(), err.c_str());
    return 1;
  }

  if (!build_log.OpenForWrite(log_path.c_str(), &err)) {
    Error("opening build log: %s", err.c_str());
    return 1;
  }

  if (!rebuilt_manifest) { // Don't get caught in an infinite loop by a rebuild
                           // target that is never up to date.
    if (RebuildManifest(&state, config, input_file, &err)) {
      rebuilt_manifest = true;
      goto reload;
    } else if (!err.empty()) {
      Error("rebuilding '%s': %s", input_file, err.c_str());
      return 1;
    }
  }

  vector<Node*> targets;
  if (!CollectTargetsFromArgs(&state, argc, argv, &targets, &err)) {
    Error("%s", err.c_str());
    return 1;
  }

  Builder builder(&state, config);
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
