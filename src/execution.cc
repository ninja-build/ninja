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

#include "public/execution.h"

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
#include <sstream>

#include "public/ui.h"

#include "browse.h"
#include "clean.h"
#include "debug_flags.h"
#include "deps_log.h"
#include "graphviz.h"
#include "manifest_parser.h"
#include "metrics.h"
#include "state.h"
#include "status.h"

namespace ninja {
namespace {

void EncodeJSONString(const char *str) {
  while (*str) {
    if (*str == '"' || *str == '\\')
      putchar('\\');
    putchar(*str);
    str++;
  }
}

std::string EvaluateCommandWithRspfile(const Edge* edge,
                                       const Execution::Options::CompilationDatabase::EvaluateCommandMode mode) {
  std::string command = edge->EvaluateCommand();
  if (mode == Execution::Options::CompilationDatabase::EvaluateCommandMode::ECM_NORMAL)
    return command;

  std::string rspfile = edge->GetUnescapedRspfile();
  if (rspfile.empty())
    return command;

  size_t index = command.find(rspfile);
  if (index == 0 || index == std::string::npos || command[index - 1] != '@')
    return command;

  std::string rspfile_content = edge->GetBinding("rspfile_content");
  size_t newline_index = 0;
  while ((newline_index = rspfile_content.find('\n', newline_index)) !=
         std::string::npos) {
    rspfile_content.replace(newline_index, 1, 1, ' ');
    ++newline_index;
  }
  command.replace(index - 1, rspfile.length() + 1, rspfile_content);
  return command;
}

/// Choose a default value for the parallelism flag.
int GuessParallelism() {
  switch (int processors = GetProcessorCount()) {
  case 0:
  case 1:
    return 2;
  case 2:
    return 3;
  default:
    return processors + 2;
  }
}

void PrintCommands(Edge* edge, EdgeSet* seen, Execution::Options::Commands::PrintCommandMode mode) {
  if (!edge)
    return;
  if (!seen->insert(edge).second)
    return;

  if (mode == Execution::Options::Commands::PrintCommandMode::PCM_All) {
    for (vector<Node*>::iterator in = edge->inputs_.begin();
         in != edge->inputs_.end(); ++in)
      PrintCommands((*in)->in_edge(), seen, mode);
  }

  if (!edge->is_phony())
    puts(edge->EvaluateCommand().c_str());
}

void printCompdb(Logger* logger,
                 const char* const directory, const Edge* const edge,
                 const Execution::Options::CompilationDatabase::EvaluateCommandMode eval_mode) {
  logger->Info("\n  {\n    \"directory\": \"");
  EncodeJSONString(directory);
  logger->Info("\",\n    \"command\": \"");
  EncodeJSONString(EvaluateCommandWithRspfile(edge, eval_mode).c_str());
  logger->Info("\",\n    \"file\": \"");
  EncodeJSONString(edge->inputs_[0]->path().c_str());
  logger->Info("\",\n    \"output\": \"");
  EncodeJSONString(edge->outputs_[0]->path().c_str());
  logger->Info("\"\n  }");
}

Node* TargetNameToNode(const State* state, const std::string& path, std::string* err) {
  uint64_t slash_bits;
  std::string canonical_path = path.c_str();
  if (!CanonicalizePath(&canonical_path, &slash_bits, err))
    return NULL;

  // Special syntax: "foo.cc^" means "the first output of foo.cc".
  bool first_dependent = false;
  if (!canonical_path.empty() && canonical_path[canonical_path.size() - 1] == '^') {
    canonical_path.resize(canonical_path.size() - 1);
    first_dependent = true;
  }

  Node* node = state->LookupNode(canonical_path);

  if (!node) {
    *err =
        "unknown target '" + path + "'";
    if (path == "clean") {
      *err += ", did you mean 'ninja -t clean'?";
    } else if (path == "help") {
      *err += ", did you mean 'ninja -h'?";
    } else {
      Node* suggestion = ui::SpellcheckNode(state, path);
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

bool TargetNamesToNodes(const State* state, const std::vector<std::string>& names,
                            std::vector<Node*>* targets, std::string* err) {
  if (names.size() == 0) {
    *targets = state->DefaultNodes(err);
    return err->empty();
  }

  for (size_t i = 0; i < names.size(); ++i) {
    Node* node = TargetNameToNode(state, names[i], err);
    if (node == NULL)
      return false;
    targets->push_back(node);
  }
  return true;
}

void PrintToolTargetsList(Logger* logger, const vector<Node*>& nodes, int depth, int indent) {
  for (vector<Node*>::const_iterator n = nodes.begin();
       n != nodes.end();
       ++n) {
    for (int i = 0; i < indent; ++i)
      logger->Info("  ");
    const std::string& target = (*n)->path();
    if ((*n)->in_edge()) {
      std::ostringstream buffer;
      buffer << target << ": " << (*n)->in_edge()->rule_->name().c_str() << std::endl;
      logger->Info(buffer.str());
      if (depth > 1 || depth <= 0)
        PrintToolTargetsList(logger, (*n)->in_edge()->inputs_, depth - 1, indent + 1);
    } else {
      logger->Info(target + "\n");
    }
  }
}

}  // namespace

Execution::Execution() : Execution(NULL, Options(), new LoggerBasic(), new StatusPrinter(config_)) {}
Execution::Execution(const char* ninja_command, Options options) :
  Execution(ninja_command, options, new LoggerBasic(), new StatusPrinter(config_)) {}
Execution::Execution(const char* ninja_command, Options options, Logger* logger) :
  Execution(ninja_command, options, logger, new StatusPrinter(config_)) {}
Execution::Execution(const char* ninja_command, Options options, Logger* logger, Status* status) :
  ninja_command_(ninja_command),
  logger_(logger),
  options_(options),
  state_(new State(logger, options.debug.explain)),
  status_(status) {
  config_.parallelism = options_.parallelism;
  // We want to go until N jobs fail, which means we should allow
  // N failures and then stop.  For N <= 0, INT_MAX is close enough
  // to infinite for most sane builds.
  config_.failures_allowed = options_.failures_allowed;
  if (options_.depfile_distinct_target_lines_should_err) {
    config_.depfile_parser_options.depfile_distinct_target_lines_action_ =
        kDepfileDistinctTargetLinesActionError;
  }

}

Execution::Options::Options() :
      depfile_distinct_target_lines_should_err(false),
      dry_run(false),
      dupe_edges_should_err(true),
      failures_allowed(1),
      input_file("build.ninja"),
      max_load_average(-0.0f),
      parallelism(GuessParallelism()),
      phony_cycle_should_err(false),
      verbose(false),
      working_dir(NULL)
      {}

Execution::Options::Clean::Clean() :
  generator(false),
  targets_are_rules(false) {}

Execution::Options::Commands::Commands() :
  mode(Execution::Options::Commands::PrintCommandMode::PCM_All) {}

Execution::Options::CompilationDatabase::CompilationDatabase() :
  eval_mode(ECM_NORMAL) {}

Execution::Options::Debug::Debug() : explain(false) {}
Execution::Options::MSVC::MSVC() :
  deps_prefix(""),
  envfile(""),
  output_filename("") {}

Execution::Options::Rules::Rules() :
  print_description(false) {}

Execution::Options::Targets::Targets() :
  depth(1),
  mode(TM_DEPTH),
  rule("") {}

int Execution::Browse() {
  if (!ChangeToWorkingDirectory()) {
    return 1;
  }
  if (!LoadParser(options_.input_file)) {
    return 1;
  }
  const char* initial_target = NULL;
  if (options_.targets.size()) {
    if (options_.targets.size() == 1) {
      initial_target = options_.targets[0].c_str();
    } else {
      logger_->Error("You can only specify a single target for 'browse'.");
      return 2;
    }
  }
  if(ninja_command_) {
    RunBrowsePython(logger_, ninja_command_, options_.input_file, initial_target);
  } else {
    logger_->Error("You must specify the 'ninja_command' parameter  in your execution to browse.");
  }
  // If we get here, the browse failed.
  return 1;
}

int Execution::Build() {
  if (!ChangeToWorkingDirectory()) {
    return 1;
  }

  if (options_.working_dir) {
    // The formatting of this string, complete with funny quotes, is
    // so Emacs can properly identify that the cwd has changed for
    // subsequent commands.
    // Don't print this if a tool is being used, so that tool output
    // can be piped into a file without this string showing up.
    std::ostringstream buffer;
    buffer << "Entering directory `" << options_.working_dir << "'" << std::endl;
    logger_->Info(buffer.str());
  }
  std::string err;
  // Limit number of rebuilds, to prevent infinite loops.
  const int kCycleLimit = 100;
  for (int cycle = 1; cycle <= kCycleLimit; ++cycle) {
    if (!LoadLogs()) {
      return 1;
    }

    // Attempt to rebuild the manifest before building anything else
    if (RebuildManifest(options_.input_file, &err)) {
      // In dry_run mode the regeneration will succeed without changing the
      // manifest forever. Better to return immediately.
      if (config_.dry_run)
        return 0;
      // Start the build over with the new manifest.
      continue;
    } else if (!err.empty()) {
      status_->Error("rebuilding '%s': %s", options_.input_file, err.c_str());
      return 1;
    }

    bool successful = DoBuild();
    if (g_metrics)
      DumpMetrics();
    return successful ? 0 : 1;
  }

  status_->Error("manifest '%s' still dirty after %d tries",
      options_.input_file, kCycleLimit);
  return 1;
}

int Execution::Clean() {
  if (!ChangeToWorkingDirectory()) {
    return 1;
  }
  if (!LoadParser(options_.input_file)) {
    return 1;
  }
  Cleaner cleaner(state_, logger_, config_, state_->disk_interface_);
  if (options_.clean_options.targets_are_rules) {
    return cleaner.CleanRules(options_.targets);
  } else if(options_.targets.size()) {
    return cleaner.CleanTargets(options_.targets);
  } else {
    return cleaner.CleanAll(options_.clean_options.generator);
  }
}

int Execution::Commands() {
  if (!ChangeToWorkingDirectory()) {
    return 1;
  }
  if (!LoadParser(options_.input_file)) {
    return 1;
  }
  EdgeSet seen;
  vector<Node*> nodes;
  std::string err;
  if (!TargetNamesToNodes(state_, options_.targets, &nodes, &err)) {
    logger_->Error(err);
    return 1;
  }

  for (vector<Node*>::iterator in = nodes.begin(); in != nodes.end(); ++in)
    PrintCommands((*in)->in_edge(), &seen, options_.commands_options.mode);

  return 0;
}

int Execution::CompilationDatabase() {
  if (!ChangeToWorkingDirectory()) {
    return 1;
  }
  if (!LoadParser(options_.input_file)) {
    return 1;
  }
  bool first = true;
  vector<char> cwd;

  do {
    cwd.resize(cwd.size() + 1024);
    errno = 0;
  } while (!getcwd(&cwd[0], cwd.size()) && errno == ERANGE);
  if (errno != 0 && errno != ERANGE) {
    std::ostringstream message;
    message << "cannot determine working directory: " << strerror(errno);
    logger_->Error(message.str());
    return 1;
  }

  putchar('[');
  for (vector<Edge*>::const_iterator e = state_->edges_.begin();
       e != state_->edges_.end(); ++e) {
    if ((*e)->inputs_.empty())
      continue;
    if (options_.targets.size()) {
      for (size_t i = 0; i != options_.targets.size(); ++i) {
        if ((*e)->rule_->name() == options_.targets[i]) {
          if (!first) {
            putchar(',');
          }
          printCompdb(logger_, &cwd[0], *e, options_.compilationdatabase_options.eval_mode);
          first = false;
        }
      }
    } else {
      if (!first) {
        putchar(',');
      }
      printCompdb(logger_, &cwd[0], *e, options_.compilationdatabase_options.eval_mode);
      first = false;
    }
  }

  puts("\n]");
  return 0;
}

int Execution::Deps() {
  if (!ChangeToWorkingDirectory()) {
    return 1;
  }
  if (!LoadLogs()) {
    return 1;
  }
  vector<Node*> nodes;
  std::string err;
  if (options_.targets.size()) {
    if (!TargetNamesToNodes(state_, options_.targets, &nodes, &err)) {
      logger_->Error(err);
      return 1;
    }
  } else {
    for (vector<Node*>::const_iterator ni = state_->deps_log_->nodes().begin();
         ni != state_->deps_log_->nodes().end(); ++ni) {
      if (state_->deps_log_->IsDepsEntryLiveFor(*ni))
        nodes.push_back(*ni);
    }
  }

  RealDiskInterface disk_interface;
  for (vector<Node*>::iterator it = nodes.begin(), end = nodes.end();
       it != end; ++it) {
    DepsLog::Deps* deps = state_->deps_log_->GetDeps(*it);
    if (!deps) {
      std::ostringstream buffer;
      buffer << (*it)->path().c_str() << ": deps not found" << std::endl;
      logger_->Info(buffer.str());
      continue;
    }

    std::string err;
    TimeStamp mtime = disk_interface.Stat((*it)->path(), &err);
    if (mtime == -1) {
      // Log and ignore Stat() errors;
      logger_->Error(err);
    }
    std::ostringstream buffer;
    buffer << (*it)->path().c_str() <<
      ": $deps " << deps->node_count <<
      ", deps mtime " << deps->mtime <<
      " (" << (!mtime || mtime > deps->mtime ? "STALE":"VALID") << ")" << std::endl;
    logger_->Info(buffer.str());
    for (int i = 0; i < deps->node_count; ++i) {
      buffer.clear();
      buffer << "    " << deps->nodes[i]->path().c_str() << std::endl;
      logger_->Info(buffer.str());
    }
    logger_->Info("\n");
  }

  return 0;
}

int Execution::Graph() {
  if (!ChangeToWorkingDirectory()) {
    return 1;
  }
  if (!LoadParser(options_.input_file)) {
    return 1;
  }
  vector<Node*> nodes;
  std::string err;
  if (!TargetNamesToNodes(state_, options_.targets, &nodes, &err)) {
    logger_->Error(err);
    return 1;
  }

  GraphViz graph(state_, state_->disk_interface_);
  graph.Start();
  for (vector<Node*>::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
    graph.AddTarget(*n);
  graph.Finish();

  return 0;
}

int Execution::MSVC() {
#if defined(_MSC_VER)
  if (!ChangeToWorkingDirectory()) {
    return 1;
  }
  return MSVCHelperMain(options_.msvc_options.deps_prefix, options_.msvc_options.envfile, options_.msvc_options.output_filename);
#else
  logger_->Error("Not supported on this platform.");
  return 1;
#endif
}

int Execution::Query() {
  if (!ChangeToWorkingDirectory()) {
    return 1;
  }
  if (!LoadLogs()) {
    return 1;
  }
  if (options_.targets.size() == 0) {
    logger_->Error("expected a target to query");
    return 1;
  }

  DyndepLoader dyndep_loader(state_, state_->disk_interface_);

  for (size_t i = 0; i < options_.targets.size(); ++i) {
    std::string err;
    std::string target_name = options_.targets[i];
    Node* node = TargetNameToNode(state_, target_name, &err);
    if (!node) {
      logger_->Error(err);
      return 1;
    }

    std::ostringstream buffer;
    buffer << node->path() << ":" << std::endl;
    logger_->Info(buffer.str());
    if (Edge* edge = node->in_edge()) {
      if (edge->dyndep_ && edge->dyndep_->dyndep_pending()) {
        if (!dyndep_loader.LoadDyndeps(edge->dyndep_, &err)) {
          logger_->Warning(err);
        }
      }
      buffer.clear();
      buffer << "  input: " << edge->rule_->name().c_str() << std::endl;
      logger_->Info(buffer.str());
      for (int in = 0; in < (int)edge->inputs_.size(); in++) {
        const char* label = "";
        if (edge->is_implicit(in))
          label = "| ";
        else if (edge->is_order_only(in))
          label = "|| ";
        buffer.clear();
        buffer << "    " << label << edge->inputs_[in]->path().c_str() << std::endl;
        logger_->Info(buffer.str());
      }
    }
    logger_->Info("  outputs:\n");
    for (vector<Edge*>::const_iterator edge = node->out_edges().begin();
         edge != node->out_edges().end(); ++edge) {
      for (vector<Node*>::iterator out = (*edge)->outputs_.begin();
           out != (*edge)->outputs_.end(); ++out) {
        buffer.clear();
        buffer << "    %s" << (*out)->path() << std::endl;
      }
    }
  }
  return 0;
}
int Execution::Recompact() {
  if (!ChangeToWorkingDirectory()) {
    return 1;
  }
  if (!LoadParser(options_.input_file)) {
    return 1;
  }
  std::string err;
  if (!EnsureBuildDirExists(&err)) {
    logger_->Error(err);
    return 1;
  }

  if (!OpenBuildLog(true, &err) ||
      !OpenDepsLog(true, &err)) {
    logger_->Error(err);
    return 1;
  }

  // Hack: OpenBuildLog()/OpenDepsLog() can return a warning via err
  if(!err.empty()) {
    logger_->Warning(err);
    err.clear();
  }

  return 0;
}

int Execution::Rules() {
  if (!ChangeToWorkingDirectory()) {
    return 1;
  }
  if (!LoadParser(options_.input_file)) {
    return 1;
  }
  typedef map<string, const Rule*> Rules;
  const Rules& rules = state_->bindings_.GetRules();
  for (Rules::const_iterator i = rules.begin(); i != rules.end(); ++i) {
    logger_->Info(i->first);
    if (options_.rules_options.print_description) {
      logger_->Info("Description!");
      const Rule* rule = i->second;
      const EvalString* description = rule->GetBinding("description");
      if (description != NULL) {
        logger_->Info(": ");
        logger_->Info(description->Unparse());
      }
    }
    logger_->Info("\n");
  }
  return 0;
}

int Execution::Targets() {
  if (!ChangeToWorkingDirectory()) {
    return 1;
  }
  if (!LoadParser(options_.input_file)) {
    return 1;
  }
  std::string err;
  vector<Node*> root_nodes = state_->RootNodes(&err);
  if (options_.targets_options.mode == Options::Targets::TargetsMode::TM_ALL) {
    ToolTargetsList();
    return 0;
  } else if (options_.targets_options.mode == Options::Targets::TargetsMode::TM_DEPTH) {
    std::ostringstream buffer;
    buffer << "Showing depth " << options_.targets_options.depth << std::endl;
    std::string err;
    vector<Node*> root_nodes = state_->RootNodes(&err);
    if (err.empty()) {
      PrintToolTargetsList(logger_, root_nodes, options_.targets_options.depth, 0);
      return 0;
    } else {
      logger_->Error(err);
      return 1;
    }
  } else if (options_.targets_options.mode == Options::Targets::TargetsMode::TM_RULE) {
    if (options_.targets_options.rule.empty()) {
      for (vector<Edge*>::const_iterator e = state_->edges_.begin();
           e != state_->edges_.end(); ++e) {
        for (vector<Node*>::iterator inps = (*e)->inputs_.begin();
             inps != (*e)->inputs_.end(); ++inps) {
          if (!(*inps)->in_edge())
            logger_->Info((*inps)->path() + "\n");
        }
      }
      return 0;
    } else {
      ToolTargetsList(options_.targets_options.rule);
      return 0;
    }
  } else {
    logger_->Error("This shouldn't be possible, a developer must have added "
      "a new option without fixing the conditional");
    return 9;
  }
}

int Execution::Urtle() {
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
      for (int i = 0; i < max(count, 1); ++i) {
        std::ostringstream buffer;
        buffer << *p;
        logger_->Info(buffer.str());
      }
      count = 0;
    }
  }
  return 0;
}

bool Execution::ChangeToWorkingDirectory() {
  if (!options_.working_dir) 
    return true;
  if (chdir(options_.working_dir) < 0) {
    std::ostringstream buffer;
    buffer << "chdir to '" << options_.working_dir << "' - " << strerror(errno) << std::endl;
    logger_->Error(buffer.str());
    return false;
  }
  return true;
}

bool Execution::DoBuild() {
  std::string err;
  state_->disk_interface_->AllowStatCache(g_experimental_statcache);

  Builder builder(state_, config_, state_->build_log_, state_->deps_log_, state_->disk_interface_,
                  status_, state_->start_time_millis_);
  for (size_t i = 0; i < options_.targets.size(); ++i) {
    if (!builder.AddTarget(options_.targets[i], &err)) {
      if (!err.empty()) {
        status_->Error("%s", err.c_str());
        return false;
      } else {
        // Added a target that is already up-to-date; not really
        // an error.
      }
    }
  }

  // Make sure restat rules do not see stale timestamps.
  state_->disk_interface_->AllowStatCache(false);

  if (builder.AlreadyUpToDate()) {
    status_->Info("no work to do.");
    return true;
  }

  if (!builder.Build(&err)) {
    status_->Info("build stopped: %s.", err.c_str());
    if (err.find("interrupted by user") != std::string::npos) {
      return false;
    }
    return false;
  }

  return true;
}

void Execution::DumpMetrics() {
  g_metrics->Report();

  logger_->Info("\n");
  int count = (int)state_->paths_.size();
  int buckets = (int)state_->paths_.bucket_count();
  std::ostringstream buffer;
  buffer << "path->node hash load " << count / (double) buckets <<
    " (" << count << " entries / " << buckets << " buckets)" << std::endl;
  logger_->Info(buffer.str());
}

bool Execution::EnsureBuildDirExists(std::string* err) {
  std::string build_dir = state_->bindings_.LookupVariable("builddir");
  if (!build_dir.empty() && !config_.dry_run) {
    if (!state_->disk_interface_->MakeDirs(build_dir + "/.") && errno != EEXIST) {
      *err = "creating build directory " + build_dir + ": " + strerror(errno);
      return false;
    }
  }
  return true;
}

bool Execution::LoadLogs() {
    if (!LoadParser(options_.input_file)) {
      return false;
    }
    std::string err;
    if (!EnsureBuildDirExists(&err))
      return false;

    if (!OpenBuildLog(false, &err) || !OpenDepsLog(false, &err)) {
      logger_->Error(err);
      return false;
    }
  return true;
}

bool Execution::LoadParser(const std::string& input_file) {
    ManifestParserOptions parser_opts;
    if (options_.dupe_edges_should_err) {
      parser_opts.dupe_edge_action_ = kDupeEdgeActionError;
    }
    if (options_.phony_cycle_should_err) {
      parser_opts.phony_cycle_action_ = kPhonyCycleActionError;
    }
    ManifestParser parser(state_, state_->disk_interface_, parser_opts);
    std::string err;
    if (!parser.Load(options_.input_file, &err)) {
      status_->Error("%s", err.c_str());
      return false;
    }
  return true;
}

bool Execution::OpenBuildLog(bool recompact_only, std::string* err) {
  /// The build directory, used for storing the build log etc.
  std::string build_dir = state_->bindings_.LookupVariable("builddir");
  std::string log_path = ".ninja_log";
  if (!build_dir.empty())
    log_path = build_dir + "/" + log_path;

  if (!state_->build_log_->Load(log_path, err)) {
    *err = "loading build log " + log_path + ": " + *err;
    return false;
  }

  if (recompact_only) {
    bool success = state_->build_log_->Recompact(log_path, *state_, err);
    if (!success)
      *err = "failed recompaction: " + *err;
    return success;
  }

  if (!config_.dry_run) {
    if (!state_->build_log_->OpenForWrite(log_path, *state_, err)) {
      *err = "opening build log: " + *err;
      return false;
    }
  }

  // Some of the functions used here can provide warnings through
  // the err parameter. So clear it.
  if(!err->empty()) {
    logger_->Warning(*err);
    err->clear();
  }

  return true;
}

/// Open the deps log: load it, then open for writing.
/// @return false on error.
bool Execution::OpenDepsLog(bool recompact_only, std::string* err) {
  std::string build_dir = state_->bindings_.LookupVariable("builddir");
  std::string path = ".ninja_deps";
  if (!build_dir.empty())
    path = build_dir + "/" + path;

  if (!state_->deps_log_->Load(path, state_, err)) {
    *err = "loading deps log " + path + ": " + *err;
    return false;
  }

  if (recompact_only) {
    bool success = state_->deps_log_->Recompact(path, err);
    if (!success)
      *err = "failed recompaction: " + *err;
    return success;
  }

  if (!config_.dry_run) {
    if (!state_->deps_log_->OpenForWrite(path, err)) {
      *err = "opening deps log: " + *err;
      return false;
    }
  }

  // Some of the functions used here can provide warnings through
  // the err parameter. So clear it.
  if(!err->empty()) {
    logger_->Warning(*err);
    err->clear();
  }

  return true;
}

/// Rebuild the build manifest, if necessary.
/// Returns true if the manifest was rebuilt.
bool Execution::RebuildManifest(const char* input_file, std::string* err) {
  std::string path = input_file;
  uint64_t slash_bits;  // Unused because this path is only used for lookup.
  if (!CanonicalizePath(&path, &slash_bits, err))
    return false;
  Node* node = state_->LookupNode(path);
  if (!node)
    return false;

  Builder builder(state_, config_, state_->build_log_, state_->deps_log_, state_->disk_interface_,
                  status_, state_->start_time_millis_);
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
    state_->Reset();
    return false;
  }

  return true;
}

void Execution::ToolTargetsList(const std::string& rule_name) {
  set<string> rules;

  // Gather the outputs.
  for (vector<Edge*>::const_iterator e = state_->edges_.begin();
       e != state_->edges_.end(); ++e) {
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
    logger_->Info((*i) + "\n");
  }
}

void Execution::ToolTargetsList() {
  for (vector<Edge*>::const_iterator e = state_->edges_.begin();
       e != state_->edges_.end(); ++e) {
    for (vector<Node*>::iterator out_node = (*e)->outputs_.begin();
         out_node != (*e)->outputs_.end(); ++out_node) {
      std::ostringstream buffer;
      buffer << (*out_node)->path() << ": " << (*e)->rule_->name() << std::endl;
      logger_->Info(buffer.str());
    }
  }
}

int TargetsList(Logger* logger, const vector<Node*>& nodes, int depth, int indent) {
  for (vector<Node*>::const_iterator n = nodes.begin();
       n != nodes.end();
       ++n) {
    std::ostringstream buffer;
    for (int i = 0; i < indent; ++i) {
      buffer << "  ";
    }
    const std::string& target = (*n)->path();
    if ((*n)->in_edge()) {
      buffer << target << ": " << (*n)->in_edge()->rule_->name() << std::endl;
      logger->Info(buffer.str());
      if (depth > 1 || depth <= 0)
        PrintToolTargetsList(logger, (*n)->in_edge()->inputs_, depth - 1, indent + 1);
    } else {
      logger->Info(target + "\n");
    }
  }
  return 0;
}

}  // namespace ninja
