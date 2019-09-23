// Copyright 2019 Google Inc. All Rights Reserved.
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

#include "public/tools.h"

#include <sstream>

#ifdef _WIN32
#include "getopt.h"
#include <direct.h>
#include <windows.h>
#elif defined(_AIX)
#include "getopt.h"
#include <unistd.h>
#else
#include <getopt.h>
#include <unistd.h>
#endif

#include "browse.h"
#include "build_log.h"
#include "clean.h"
#include "debug_flags.h"
#include "deps_log.h"
#include "disk_interface.h"
#include "edit_distance.h"
#include "graphviz.h"
#include "state.h"
#include "status.h"


namespace ninja {

Node* SpellcheckNode(State* state, const std::string& path) {
  const bool kAllowReplacements = true;
  const int kMaxValidEditDistance = 3;

  int min_distance = kMaxValidEditDistance + 1;
  Node* result = NULL;
  for (State::Paths::iterator i = state->paths_.begin(); i != state->paths_.end(); ++i) {
    int distance = EditDistance(
        i->first, path, kAllowReplacements, kMaxValidEditDistance);
    if (distance < min_distance && i->second) {
      min_distance = distance;
      result = i->second;
    }
  }
  return result;
}

Node* CollectTarget(State* state, const char* cpath, std::string* err) {
  std::string path = cpath;
  uint64_t slash_bits;
  if (!CanonicalizePath(&path, &slash_bits, err))
    return NULL;

  // Special syntax: "foo.cc^" means "the first output of foo.cc".
  bool first_dependent = false;
  if (!path.empty() && path[path.size() - 1] == '^') {
    path.resize(path.size() - 1);
    first_dependent = true;
  }

  Node* node = state->LookupNode(path);

  if (!node) {
    *err =
        "unknown target '" + Node::PathDecanonicalized(path, slash_bits) + "'";
    if (path == "clean") {
      *err += ", did you mean 'ninja -t clean'?";
    } else if (path == "help") {
      *err += ", did you mean 'ninja -h'?";
    } else {
      Node* suggestion = SpellcheckNode(state, path);
      if (suggestion) {
        *err += ", did you mean '" + suggestion->path() + "'?";
      }
    }
    return NULL;
  }

  if (!first_dependent) {
    return node;
  }

  if (node->out_edges().empty()) {
    *err = "'" + path + "' has no out edge";
    return NULL;
  }

  Edge* edge = node->out_edges()[0];
  if (edge->outputs_.empty()) {
    edge->Dump();
    *err = "edge has no outputs";
    return NULL;
  }
  return edge->outputs_[0];
}

bool CollectTargetsFromArgs(State* state, int argc, char* argv[],
                            std::vector<Node*>* targets, std::string* err) {
  if (argc == 0) {
    *targets = state->DefaultNodes(err);
    return err->empty();
  }

  for (int i = 0; i < argc; ++i) {
    Node* node = CollectTarget(state, argv[i], err);
    if (node == NULL)
      return false;
    targets->push_back(node);
  }
  return true;
}

bool EnsureBuildDirExists(State* state, RealDiskInterface* disk_interface, const BuildConfig& build_config, std::string* err) {
  std::string build_dir = state->bindings_.LookupVariable("builddir");
  if (!build_dir.empty() && !build_config.dry_run) {
    if (!disk_interface->MakeDirs(build_dir + "/.") && errno != EEXIST) {
      *err = "creating build directory " + build_dir + ": " + strerror(errno);
      return false;
    }
  }
  return true;
}

bool OpenBuildLog(State* state, const BuildConfig& build_config, bool recompact_only, std::string* err) {
  /// The build directory, used for storing the build log etc.
  std::string build_dir = state->bindings_.LookupVariable("builddir");
  string log_path = ".ninja_log";
  if (!build_dir.empty())
    log_path = build_dir + "/" + log_path;

  if (!state->build_log_->Load(log_path, err)) {
    *err = "loading build log " + log_path + ": " + *err;
    return false;
  }

  if (recompact_only) {
    bool success = state->build_log_->Recompact(log_path, *state, err);
    if (!success)
      *err = "failed recompaction: " + *err;
    return success;
  }

  if (!build_config.dry_run) {
    if (!state->build_log_->OpenForWrite(log_path, *state, err)) {
      *err = "opening build log: " + *err;
      return false;
    }
  }

  return true;
}

/// Open the deps log: load it, then open for writing.
/// @return false on error.
bool OpenDepsLog(State* state, const BuildConfig& build_config, bool recompact_only, std::string* err) {
  std::string build_dir = state->bindings_.LookupVariable("builddir");
  std::string path = ".ninja_deps";
  if (!build_dir.empty())
    path = build_dir + "/" + path;

  if (!state->deps_log_->Load(path, state, err)) {
    *err = "loading deps log " + path + ": " + *err;
    return false;
  }

  if (recompact_only) {
    bool success = state->deps_log_->Recompact(path, err);
    if (!success)
      *err = "failed recompaction: " + *err;
    return success;
  }

  if (!build_config.dry_run) {
    if (!state->deps_log_->OpenForWrite(path, err)) {
      *err = "opening deps log: " + *err;
      return false;
    }
  }

  return true;
}

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

void EncodeJSONString(const char *str) {
  while (*str) {
    if (*str == '"' || *str == '\\')
      putchar('\\');
    putchar(*str);
    str++;
  }
}

enum EvaluateCommandMode {
  ECM_NORMAL,
  ECM_EXPAND_RSPFILE
};
std::string EvaluateCommandWithRspfile(const Edge* edge,
                                       const EvaluateCommandMode mode) {
  string command = edge->EvaluateCommand();
  if (mode == ECM_NORMAL)
    return command;

  string rspfile = edge->GetUnescapedRspfile();
  if (rspfile.empty())
    return command;

  size_t index = command.find(rspfile);
  if (index == 0 || index == string::npos || command[index - 1] != '@')
    return command;

  string rspfile_content = edge->GetBinding("rspfile_content");
  size_t newline_index = 0;
  while ((newline_index = rspfile_content.find('\n', newline_index)) !=
         string::npos) {
    rspfile_content.replace(newline_index, 1, 1, ' ');
    ++newline_index;
  }
  command.replace(index - 1, rspfile.length() + 1, rspfile_content);
  return command;
}

void printCompdb(const char* const directory, const Edge* const edge,
                 const EvaluateCommandMode eval_mode) {
  printf("\n  {\n    \"directory\": \"");
  EncodeJSONString(directory);
  printf("\",\n    \"command\": \"");
  EncodeJSONString(EvaluateCommandWithRspfile(edge, eval_mode).c_str());
  printf("\",\n    \"file\": \"");
  EncodeJSONString(edge->inputs_[0]->path().c_str());
  printf("\",\n    \"output\": \"");
  EncodeJSONString(edge->outputs_[0]->path().c_str());
  printf("\"\n  }");
}

enum PrintCommandMode { PCM_Single, PCM_All };
void PrintCommands(Edge* edge, EdgeSet* seen, PrintCommandMode mode) {
  if (!edge)
    return;
  if (!seen->insert(edge).second)
    return;

  if (mode == PCM_All) {
    for (vector<Node*>::iterator in = edge->inputs_.begin();
         in != edge->inputs_.end(); ++in)
      PrintCommands((*in)->in_edge(), seen, mode);
  }

  if (!edge->is_phony())
    puts(edge->EvaluateCommand().c_str());
}

int TargetsList(const vector<Node*>& nodes, int depth, int indent) {
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

/// Rebuild the build manifest, if necessary.
/// Returns true if the manifest was rebuilt.
bool RebuildManifest(State* state, const char* input_file, string* err,
                                Status* status) {
  string path = input_file;
  uint64_t slash_bits;  // Unused because this path is only used for lookup.
  if (!CanonicalizePath(&path, &slash_bits, err))
    return false;
  Node* node = state->LookupNode(path);
  if (!node)
    return false;

  Builder builder(state, state->config_, state->build_log_, state->deps_log_, state->disk_interface_,
                  status, state->start_time_millis_);
  if (!builder.AddTarget(node, err))
    return false;

  if (builder.AlreadyUpToDate())
    return false;  // Not an error, but we didn't rebuild.

  if (!builder.Build(err))
    return false;

  // The manifest was only rebuilt if it is now dirty (it may have been cleaned
  // by a restat).
  if (!node->dirty()) {
    // Reset the state to prevent problems like
    // https://github.com/ninja-build/ninja/issues/874
    state->Reset();
    return false;
  }

  return true;
}


int RunBuild(State* state, int argc, char** argv, Status* status) {
  string err;
  vector<Node*> targets;
  if (!CollectTargetsFromArgs(state, argc, argv, &targets, &err)) {
    status->Error("%s", err.c_str());
    return 1;
  }

  state->disk_interface_->AllowStatCache(g_experimental_statcache);

  Builder builder(state, state->config_, state->build_log_, state->deps_log_, state->disk_interface_,
                  status, state->start_time_millis_);
  for (size_t i = 0; i < targets.size(); ++i) {
    if (!builder.AddTarget(targets[i], &err)) {
      if (!err.empty()) {
        status->Error("%s", err.c_str());
        return 1;
      } else {
        // Added a target that is already up-to-date; not really
        // an error.
      }
    }
  }

  // Make sure restat rules do not see stale timestamps.
  state->disk_interface_->AllowStatCache(false);

  if (builder.AlreadyUpToDate()) {
    status->Info("no work to do.");
    return 0;
  }

  if (!builder.Build(&err)) {
    status->Info("build stopped: %s.", err.c_str());
    if (err.find("interrupted by user") != string::npos) {
      return 2;
    }
    return 1;
  }

  return 0;
}

namespace tool {
#if defined(NINJA_HAVE_BROWSE)
int Browse(State* state, const Options* options, int argc, char* argv[]) {
  RunBrowsePython(state, state->ninja_command_, options->input_file, argc, argv);
  // If we get here, the browse failed.
  return 1;
}
#else
int Browse(State* state, const Options*, int, char**) {
  std::cerr << "browse tool not supported on this platform" << std::endl;
  ExitNow();
  // Never reached
  return 1;
}
#endif

int Clean(State* state, const Options* options, int argc, char* argv[]) {
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
    state->Log(Logger::Level::ERROR, "expected a rule to clean");
    return 1;
  }

  Cleaner cleaner(state, state->config_, state->disk_interface_);
  if (argc >= 1) {
    if (clean_rules)
      return cleaner.CleanRules(argc, argv);
    else
      return cleaner.CleanTargets(argc, argv);
  } else {
    return cleaner.CleanAll(generator);
  }
}

int Commands(State* state, const Options* options, int argc, char* argv[]) {
  // The clean tool uses getopt, and expects argv[0] to contain the name of
  // the tool, i.e. "commands".
  ++argc;
  --argv;

  PrintCommandMode mode = PCM_All;

  optind = 1;
  int opt;
  while ((opt = getopt(argc, argv, const_cast<char*>("hs"))) != -1) {
    switch (opt) {
    case 's':
      mode = PCM_Single;
      break;
    case 'h':
    default:
      printf("usage: ninja -t commands [options] [targets]\n"
"\n"
"options:\n"
"  -s     only print the final command to build [target], not the whole chain\n"
             );
    return 1;
    }
  }
  argv += optind;
  argc -= optind;

  vector<Node*> nodes;
  string err;
  if (!CollectTargetsFromArgs(state, argc, argv, &nodes, &err)) {
    state->Log(Logger::Level::ERROR, err);
    return 1;
  }

  EdgeSet seen;
  for (vector<Node*>::iterator in = nodes.begin(); in != nodes.end(); ++in)
    PrintCommands((*in)->in_edge(), &seen, mode);

  return 0;
}

int CompilationDatabase(State* state, const Options* options, int argc,
                                       char* argv[]) {
  // The compdb tool uses getopt, and expects argv[0] to contain the name of
  // the tool, i.e. "compdb".
  argc++;
  argv--;

  EvaluateCommandMode eval_mode = ECM_NORMAL;

  optind = 1;
  int opt;
  while ((opt = getopt(argc, argv, const_cast<char*>("hx"))) != -1) {
    switch(opt) {
      case 'x':
        eval_mode = ECM_EXPAND_RSPFILE;
        break;

      case 'h':
      default:
        printf(
            "usage: ninja -t compdb [options] [rules]\n"
            "\n"
            "options:\n"
            "  -x     expand @rspfile style response file invocations\n"
            );
        return 1;
    }
  }
  argv += optind;
  argc -= optind;

  bool first = true;
  vector<char> cwd;

  do {
    cwd.resize(cwd.size() + 1024);
    errno = 0;
  } while (!getcwd(&cwd[0], cwd.size()) && errno == ERANGE);
  if (errno != 0 && errno != ERANGE) {
    std::ostringstream message;
    message << "cannot determine working directory: " << strerror(errno);
    state->Log(Logger::Level::ERROR, message.str());
    return 1;
  }

  putchar('[');
  for (vector<Edge*>::iterator e = state->edges_.begin();
       e != state->edges_.end(); ++e) {
    if ((*e)->inputs_.empty())
      continue;
    if (argc == 0) {
      if (!first) {
        putchar(',');
      }
      printCompdb(&cwd[0], *e, eval_mode);
      first = false;
    } else {
      for (int i = 0; i != argc; ++i) {
        if ((*e)->rule_->name() == argv[i]) {
          if (!first) {
            putchar(',');
          }
          printCompdb(&cwd[0], *e, eval_mode);
          first = false;
        }
      }
    }
  }

  puts("\n]");
  return 0;
}

int Deps(State* state, const Options* options, int argc, char** argv) {
  vector<Node*> nodes;
  if (argc == 0) {
    for (vector<Node*>::const_iterator ni = state->deps_log_->nodes().begin();
         ni != state->deps_log_->nodes().end(); ++ni) {
      if (state->deps_log_->IsDepsEntryLiveFor(*ni))
        nodes.push_back(*ni);
    }
  } else {
    string err;
    if (!CollectTargetsFromArgs(state, argc, argv, &nodes, &err)) {
      state->Log(Logger::Level::ERROR, err);
      return 1;
    }
  }

  RealDiskInterface disk_interface;
  for (vector<Node*>::iterator it = nodes.begin(), end = nodes.end();
       it != end; ++it) {
    DepsLog::Deps* deps = state->deps_log_->GetDeps(*it);
    if (!deps) {
      printf("%s: deps not found\n", (*it)->path().c_str());
      continue;
    }

    string err;
    TimeStamp mtime = disk_interface.Stat((*it)->path(), &err);
    if (mtime == -1) {
      // Log and ignore Stat() errors;
      state->Log(Logger::Level::ERROR, err);
    }
    printf("%s: #deps %d, deps mtime %" PRId64 " (%s)\n",
           (*it)->path().c_str(), deps->node_count, deps->mtime,
           (!mtime || mtime > deps->mtime ? "STALE":"VALID"));
    for (int i = 0; i < deps->node_count; ++i)
      printf("    %s\n", deps->nodes[i]->path().c_str());
    printf("\n");
  }

  return 0;
}

int Graph(State* state, const Options* options, int argc, char* argv[]) {
  vector<Node*> nodes;
  string err;
  if (!CollectTargetsFromArgs(state, argc, argv, &nodes, &err)) {
    state->Log(Logger::Level::ERROR, err);
    return 1;
  }

  GraphViz graph(state, state->disk_interface_);
  graph.Start();
  for (vector<Node*>::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
    graph.AddTarget(*n);
  graph.Finish();

  return 0;
}

int Query(State* state, const Options* options, int argc, char* argv[]) {
  if (argc == 0) {
    state->Log(Logger::Level::ERROR, "expected a target to query");
    return 1;
  }

  DyndepLoader dyndep_loader(state, state->disk_interface_);

  for (int i = 0; i < argc; ++i) {
    string err;
    Node* node = CollectTarget(state, argv[i], &err);
    if (!node) {
      state->Log(Logger::Level::ERROR, err);
      return 1;
    }

    printf("%s:\n", node->path().c_str());
    if (Edge* edge = node->in_edge()) {
      if (edge->dyndep_ && edge->dyndep_->dyndep_pending()) {
        if (!dyndep_loader.LoadDyndeps(edge->dyndep_, &err)) {
          state->Log(Logger::Level::WARNING, err);
        }
      }
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

int Recompact(State* state, const Options* options, int argc, char* argv[]) {
  string err;
  if (!EnsureBuildDirExists(state, state->disk_interface_, state->config_, &err)) {
    state->Log(Logger::Level::ERROR, err);
    return 1;
  }

  if (!OpenBuildLog(state, state->config_, true, &err) ||
      !OpenDepsLog(state, state->config_, true, &err)) {
    state->Log(Logger::Level::ERROR, err);
    return 1;
  }

  // Hack: OpenBuildLog()/OpenDepsLog() can return a warning via err
  if(!err.empty()) {
    state->Log(Logger::Level::WARNING, err);
    err.clear();
  }

  return 0;
}

int Rules(State* state, const Options* options, int argc, char* argv[]) {
  // Parse options.

  // The rules tool uses getopt, and expects argv[0] to contain the name of
  // the tool, i.e. "rules".
  argc++;
  argv--;

  bool print_description = false;

  optind = 1;
  int opt;
  while ((opt = getopt(argc, argv, const_cast<char*>("hd"))) != -1) {
    switch (opt) {
    case 'd':
      print_description = true;
      break;
    case 'h':
    default:
      printf("usage: ninja -t rules [options]\n"
             "\n"
             "options:\n"
             "  -d     also print the description of the rule\n"
             "  -h     print this message\n"
             );
    return 1;
    }
  }
  argv += optind;
  argc -= optind;

  // Print rules

  typedef map<string, const Rule*> Rules;
  const Rules& rules = state->bindings_.GetRules();
  for (Rules::const_iterator i = rules.begin(); i != rules.end(); ++i) {
    printf("%s", i->first.c_str());
    if (print_description) {
      const Rule* rule = i->second;
      const EvalString* description = rule->GetBinding("description");
      if (description != NULL) {
        printf(": %s", description->Unparse().c_str());
      }
    }
    printf("\n");
  }
  return 0;
}

int Targets(State* state, const Options* options, int argc, char* argv[]) {
  int depth = 1;
  if (argc >= 1) {
    string mode = argv[0];
    if (mode == "rule") {
      string rule;
      if (argc > 1)
        rule = argv[1];
      if (rule.empty())
        return ToolTargetsSourceList(state);
      else
        return ToolTargetsList(state, rule);
    } else if (mode == "depth") {
      if (argc > 1)
        depth = atoi(argv[1]);
    } else if (mode == "all") {
      return ToolTargetsList(state);
    } else {
      const char* suggestion =
          SpellcheckString(mode.c_str(), "rule", "depth", "all", NULL);

      ostringstream message;
      message << "unknown target tool mode '" << mode << "'";
      if (suggestion) {
        message << ", did you mean '" << suggestion << "'?";
      }
      state->Log(Logger::Level::ERROR, message.str());
      return 1;
    }
  }

  string err;
  vector<Node*> root_nodes = state->RootNodes(&err);
  if (err.empty()) {
    return ToolTargetsList(root_nodes, depth, 0);
  } else {
    state->Log(Logger::Level::ERROR, err);
    return 1;
  }
}

int Urtle(State* state, const Options* options, int argc, char** argv) {
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
      for (int i = 0; i < max(count, 1); ++i)
        printf("%c", *p);
      count = 0;
    }
  }
  return 0;
}

#if defined(_MSC_VER)
int MSVC(State* state, const Options* options, int argc, char* argv[]) {
  // Reset getopt: push one argument onto the front of argv, reset optind.
  argc++;
  argv--;
  optind = 0;
  return MSVCHelperMain(argc, argv);
}
#endif

}  // namespace tool
}  // namespace ninja
