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

#include "build.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <functional>

#if defined(__SVR4) && defined(__sun)
#include <sys/termios.h>
#endif

#include "build_log.h"
#include "debug_flags.h"
#include "depfile_parser.h"
#include "deps_log.h"
#include "disk_interface.h"
#include "graph.h"
#include "msvc_helper.h"
#include "state.h"
#include "subprocess.h"
#include "util.h"

namespace {

/// A CommandRunner that doesn't actually run the commands.
struct DryRunCommandRunner : public CommandRunner {
  virtual ~DryRunCommandRunner() {}

  // Overridden from CommandRunner:
  virtual bool CanRunMore();
  virtual bool StartCommand(Edge* edge);
  virtual bool WaitForCommand(Result* result);

 private:
  queue<Edge*> finished_;
};

bool DryRunCommandRunner::CanRunMore() {
  return true;
}

bool DryRunCommandRunner::StartCommand(Edge* edge) {
  finished_.push(edge);
  return true;
}

bool DryRunCommandRunner::WaitForCommand(Result* result) {
   if (finished_.empty())
     return false;

   result->status = ExitSuccess;
   result->edge = finished_.front();
   finished_.pop();
   return true;
}

}  // namespace

BuildStatus::BuildStatus(const BuildConfig& config)
    : config_(config),
      start_time_millis_(GetTimeMillis()),
      started_edges_(0), finished_edges_(0), total_edges_(0),
      progress_status_format_(NULL),
      overall_rate_(), current_rate_(config.parallelism) {

  // Don't do anything fancy in verbose mode.
  if (config_.verbosity != BuildConfig::NORMAL)
    printer_.set_smart_terminal(false);

  progress_status_format_ = getenv("NINJA_STATUS");
  if (!progress_status_format_)
    progress_status_format_ = "[%s/%t] ";
}

void BuildStatus::PlanHasTotalEdges(int total) {
  total_edges_ = total;
}

void BuildStatus::BuildEdgeStarted(Edge* edge) {
  int start_time = (int)(GetTimeMillis() - start_time_millis_);
  running_edges_.insert(make_pair(edge, start_time));
  ++started_edges_;

  PrintStatus(edge);
}

void BuildStatus::BuildEdgeFinished(Edge* edge,
                                    bool success,
                                    const string& output,
                                    int* start_time,
                                    int* end_time) {
  int64_t now = GetTimeMillis();
  ++finished_edges_;

  RunningEdgeMap::iterator i = running_edges_.find(edge);
  *start_time = i->second;
  *end_time = (int)(now - start_time_millis_);
  running_edges_.erase(i);

  if (config_.verbosity == BuildConfig::QUIET)
    return;

  if (printer_.is_smart_terminal())
    PrintStatus(edge);

  // Print the command that is spewing before printing its output.
  if (!success)
    printer_.PrintOnNewLine("FAILED: " + edge->EvaluateCommand() + "\n");

  if (!output.empty()) {
    // ninja sets stdout and stderr of subprocesses to a pipe, to be able to
    // check if the output is empty. Some compilers, e.g. clang, check
    // isatty(stderr) to decide if they should print colored output.
    // To make it possible to use colored output with ninja, subprocesses should
    // be run with a flag that forces them to always print color escape codes.
    // To make sure these escape codes don't show up in a file if ninja's output
    // is piped to a file, ninja strips ansi escape codes again if it's not
    // writing to a |smart_terminal_|.
    // (Launching subprocesses in pseudo ttys doesn't work because there are
    // only a few hundred available on some systems, and ninja can launch
    // thousands of parallel compile commands.)
    // TODO: There should be a flag to disable escape code stripping.
    string final_output;
    if (!printer_.is_smart_terminal())
      final_output = StripAnsiEscapeCodes(output);
    else
      final_output = output;
    printer_.PrintOnNewLine(final_output);
  }
}

void BuildStatus::BuildFinished() {
  printer_.PrintOnNewLine("");
}

string BuildStatus::FormatProgressStatus(
    const char* progress_status_format) const {
  string out;
  char buf[32];
  int percent;
  for (const char* s = progress_status_format; *s != '\0'; ++s) {
    if (*s == '%') {
      ++s;
      switch (*s) {
      case '%':
        out.push_back('%');
        break;

        // Started edges.
      case 's':
        snprintf(buf, sizeof(buf), "%d", started_edges_);
        out += buf;
        break;

        // Total edges.
      case 't':
        snprintf(buf, sizeof(buf), "%d", total_edges_);
        out += buf;
        break;

        // Running edges.
      case 'r':
        snprintf(buf, sizeof(buf), "%d", started_edges_ - finished_edges_);
        out += buf;
        break;

        // Unstarted edges.
      case 'u':
        snprintf(buf, sizeof(buf), "%d", total_edges_ - started_edges_);
        out += buf;
        break;

        // Finished edges.
      case 'f':
        snprintf(buf, sizeof(buf), "%d", finished_edges_);
        out += buf;
        break;

        // Overall finished edges per second.
      case 'o':
        overall_rate_.UpdateRate(finished_edges_);
        snprinfRate(overall_rate_.rate(), buf, "%.1f");
        out += buf;
        break;

        // Current rate, average over the last '-j' jobs.
      case 'c':
        current_rate_.UpdateRate(finished_edges_);
        snprinfRate(current_rate_.rate(), buf, "%.1f");
        out += buf;
        break;

        // Percentage
      case 'p':
        percent = (100 * started_edges_) / total_edges_;
        snprintf(buf, sizeof(buf), "%3i%%", percent);
        out += buf;
        break;

      case 'e': {
        double elapsed = overall_rate_.Elapsed();
        snprintf(buf, sizeof(buf), "%.3f", elapsed);
        out += buf;
        break;
      }

      default:
        Fatal("unknown placeholder '%%%c' in $NINJA_STATUS", *s);
        return "";
      }
    } else {
      out.push_back(*s);
    }
  }

  return out;
}

void BuildStatus::PrintStatus(Edge* edge) {
  if (config_.verbosity == BuildConfig::QUIET)
    return;

  bool force_full_command = config_.verbosity == BuildConfig::VERBOSE;

  string to_print = edge->GetBinding("description");
  if (to_print.empty() || force_full_command)
    to_print = edge->GetBinding("command");

  if (finished_edges_ == 0) {
    overall_rate_.Restart();
    current_rate_.Restart();
  }
  to_print = FormatProgressStatus(progress_status_format_) + to_print;

  printer_.Print(to_print,
                 force_full_command ? LinePrinter::FULL : LinePrinter::ELIDE);
}

Plan::Plan() : command_edges_(0), wanted_edges_(0) {}

bool Plan::AddTarget(Node* node, string* err) {
  vector<Node*> stack;
  return AddSubTarget(node, &stack, err);
}

bool Plan::AddSubTarget(Node* node, vector<Node*>* stack, string* err) {
  Edge* edge = node->in_edge();
  if (!edge) {  // Leaf node.
    if (node->dirty()) {
      string referenced;
      if (!stack->empty())
        referenced = ", needed by '" + stack->back()->path() + "',";
      *err = "'" + node->path() + "'" + referenced + " missing "
             "and no known rule to make it";
    }
    return false;
  }

  if (CheckDependencyCycle(node, stack, err))
    return false;

  if (edge->outputs_ready())
    return false;  // Don't need to do anything.

  // If an entry in want_ does not already exist for edge, create an entry which
  // maps to false, indicating that we do not want to build this entry itself.
  pair<map<Edge*, bool>::iterator, bool> want_ins =
    want_.insert(make_pair(edge, false));
  bool& want = want_ins.first->second;

  // If we do need to build edge and we haven't already marked it as wanted,
  // mark it now.
  if (node->dirty() && !want) {
    want = true;
    ++wanted_edges_;
    if (edge->AllInputsReady())
      ScheduleWork(edge);
    if (!edge->is_phony())
      ++command_edges_;
  }

  if (!want_ins.second)
    return true;  // We've already processed the inputs.

  stack->push_back(node);
  for (vector<Node*>::iterator i = edge->inputs_.begin();
       i != edge->inputs_.end(); ++i) {
    if (!AddSubTarget(*i, stack, err) && !err->empty())
      return false;
  }
  assert(stack->back() == node);
  stack->pop_back();

  return true;
}

bool Plan::CheckDependencyCycle(Node* node, vector<Node*>* stack, string* err) {
  vector<Node*>::reverse_iterator ri =
      find(stack->rbegin(), stack->rend(), node);
  if (ri == stack->rend())
    return false;

  // Add this node onto the stack to make it clearer where the loop
  // is.
  stack->push_back(node);

  vector<Node*>::iterator start = find(stack->begin(), stack->end(), node);
  *err = "dependency cycle: ";
  for (vector<Node*>::iterator i = start; i != stack->end(); ++i) {
    if (i != start)
      err->append(" -> ");
    err->append((*i)->path());
  }
  return true;
}

Edge* Plan::FindWork() {
  if (ready_.empty())
    return NULL;
  set<Edge*>::iterator i = ready_.begin();
  Edge* edge = *i;
  ready_.erase(i);
  return edge;
}

void Plan::ScheduleWork(Edge* edge) {
  Pool* pool = edge->pool();
  if (pool->ShouldDelayEdge()) {
    // The graph is not completely clean. Some Nodes have duplicate Out edges.
    // We need to explicitly ignore these here, otherwise their work will get
    // scheduled twice (see https://github.com/martine/ninja/pull/519)
    if (ready_.count(edge)) {
      return;
    }
    pool->DelayEdge(edge);
    pool->RetrieveReadyEdges(&ready_);
  } else {
    pool->EdgeScheduled(*edge);
    ready_.insert(edge);
  }
}

void Plan::ResumeDelayedJobs(Edge* edge) {
  edge->pool()->EdgeFinished(*edge);
  edge->pool()->RetrieveReadyEdges(&ready_);
}

void Plan::EdgeFinished(Edge* edge) {
  map<Edge*, bool>::iterator i = want_.find(edge);
  assert(i != want_.end());
  if (i->second)
    --wanted_edges_;
  want_.erase(i);
  edge->outputs_ready_ = true;

  // See if this job frees up any delayed jobs
  ResumeDelayedJobs(edge);

  // Check off any nodes we were waiting for with this edge.
  for (vector<Node*>::iterator i = edge->outputs_.begin();
       i != edge->outputs_.end(); ++i) {
    NodeFinished(*i);
  }
}

void Plan::NodeFinished(Node* node) {
  // See if we we want any edges from this node.
  for (vector<Edge*>::const_iterator i = node->out_edges().begin();
       i != node->out_edges().end(); ++i) {
    map<Edge*, bool>::iterator want_i = want_.find(*i);
    if (want_i == want_.end())
      continue;

    // See if the edge is now ready.
    if ((*i)->AllInputsReady()) {
      if (want_i->second) {
        ScheduleWork(*i);
      } else {
        // We do not need to build this edge, but we might need to build one of
        // its dependents.
        EdgeFinished(*i);
      }
    }
  }
}

void Plan::CleanNode(DependencyScan* scan, Node* node) {
  node->set_dirty(false);

  for (vector<Edge*>::const_iterator ei = node->out_edges().begin();
       ei != node->out_edges().end(); ++ei) {
    // Don't process edges that we don't actually want.
    map<Edge*, bool>::iterator want_i = want_.find(*ei);
    if (want_i == want_.end() || !want_i->second)
      continue;

    // Don't attempt to clean an edge if it failed to load deps.
    if ((*ei)->deps_missing_)
      continue;

    // If all non-order-only inputs for this edge are now clean,
    // we might have changed the dirty state of the outputs.
    vector<Node*>::iterator
        begin = (*ei)->inputs_.begin(),
        end = (*ei)->inputs_.end() - (*ei)->order_only_deps_;
    if (find_if(begin, end, mem_fun(&Node::dirty)) == end) {
      // Recompute most_recent_input.
      Node* most_recent_input = NULL;
      for (vector<Node*>::iterator ni = begin; ni != end; ++ni) {
        if (!most_recent_input || (*ni)->mtime() > most_recent_input->mtime())
          most_recent_input = *ni;
      }

      // Now, this edge is dirty if any of the outputs are dirty.
      // If the edge isn't dirty, clean the outputs and mark the edge as not
      // wanted.
      if (!scan->RecomputeOutputsDirty(*ei, most_recent_input)) {
        for (vector<Node*>::iterator ni = (*ei)->outputs_.begin();
             ni != (*ei)->outputs_.end(); ++ni) {
          CleanNode(scan, *ni);
        }

        want_i->second = false;
        --wanted_edges_;
        if (!(*ei)->is_phony())
          --command_edges_;
      }
    }
  }
}

void Plan::Dump() {
  printf("pending: %d\n", (int)want_.size());
  for (map<Edge*, bool>::iterator i = want_.begin(); i != want_.end(); ++i) {
    if (i->second)
      printf("want ");
    i->first->Dump();
  }
  printf("ready: %d\n", (int)ready_.size());
}

struct RealCommandRunner : public CommandRunner {
  explicit RealCommandRunner(const BuildConfig& config) : config_(config) {}
  virtual ~RealCommandRunner() {}
  virtual bool CanRunMore();
  virtual bool StartCommand(Edge* edge);
  virtual bool WaitForCommand(Result* result);
  virtual vector<Edge*> GetActiveEdges();
  virtual void Abort();

  const BuildConfig& config_;
  SubprocessSet subprocs_;
  map<Subprocess*, Edge*> subproc_to_edge_;
};

vector<Edge*> RealCommandRunner::GetActiveEdges() {
  vector<Edge*> edges;
  for (map<Subprocess*, Edge*>::iterator i = subproc_to_edge_.begin();
       i != subproc_to_edge_.end(); ++i)
    edges.push_back(i->second);
  return edges;
}

void RealCommandRunner::Abort() {
  subprocs_.Clear();
}

bool RealCommandRunner::CanRunMore() {
  return ((int)subprocs_.running_.size()) < config_.parallelism
    && ((subprocs_.running_.empty() || config_.max_load_average <= 0.0f)
        || GetLoadAverage() < config_.max_load_average);
}

bool RealCommandRunner::StartCommand(Edge* edge) {
  string command = edge->EvaluateCommand();
  Subprocess* subproc = subprocs_.Add(command);
  if (!subproc)
    return false;
  subproc_to_edge_.insert(make_pair(subproc, edge));

  return true;
}

bool RealCommandRunner::WaitForCommand(Result* result) {
  Subprocess* subproc;
  while ((subproc = subprocs_.NextFinished()) == NULL) {
    bool interrupted = subprocs_.DoWork();
    if (interrupted)
      return false;
  }

  result->status = subproc->Finish();
  result->output = subproc->GetOutput();

  map<Subprocess*, Edge*>::iterator i = subproc_to_edge_.find(subproc);
  result->edge = i->second;
  subproc_to_edge_.erase(i);

  delete subproc;
  return true;
}

Builder::Builder(State* state, const BuildConfig& config,
                 BuildLog* build_log, DepsLog* deps_log,
                 DiskInterface* disk_interface)
    : state_(state), config_(config), disk_interface_(disk_interface),
      scan_(state, build_log, deps_log, disk_interface) {
  status_ = new BuildStatus(config);
}

Builder::~Builder() {
  Cleanup();
}

void Builder::Cleanup() {
  if (command_runner_.get()) {
    vector<Edge*> active_edges = command_runner_->GetActiveEdges();
    command_runner_->Abort();

    for (vector<Edge*>::iterator i = active_edges.begin();
         i != active_edges.end(); ++i) {
      string depfile = (*i)->GetBinding("depfile");
      for (vector<Node*>::iterator ni = (*i)->outputs_.begin();
           ni != (*i)->outputs_.end(); ++ni) {
        // Only delete this output if it was actually modified.  This is
        // important for things like the generator where we don't want to
        // delete the manifest file if we can avoid it.  But if the rule
        // uses a depfile, always delete.  (Consider the case where we
        // need to rebuild an output because of a modified header file
        // mentioned in a depfile, and the command touches its depfile
        // but is interrupted before it touches its output file.)
        if (!depfile.empty() ||
            (*ni)->mtime() != disk_interface_->Stat((*ni)->path())) {
          disk_interface_->RemoveFile((*ni)->path());
        }
      }
      if (!depfile.empty())
        disk_interface_->RemoveFile(depfile);
    }
  }
}

Node* Builder::AddTarget(const string& name, string* err) {
  Node* node = state_->LookupNode(name);
  if (!node) {
    *err = "unknown target: '" + name + "'";
    return NULL;
  }
  if (!AddTarget(node, err))
    return NULL;
  return node;
}

bool Builder::AddTarget(Node* node, string* err) {
  node->StatIfNecessary(disk_interface_);
  if (Edge* in_edge = node->in_edge()) {
    if (!scan_.RecomputeDirty(in_edge, err))
      return false;
    if (in_edge->outputs_ready())
      return true;  // Nothing to do.
  }

  if (!plan_.AddTarget(node, err))
    return false;

  return true;
}

bool Builder::AlreadyUpToDate() const {
  return !plan_.more_to_do();
}

bool Builder::Build(string* err) {
  assert(!AlreadyUpToDate());

  status_->PlanHasTotalEdges(plan_.command_edge_count());
  int pending_commands = 0;
  int failures_allowed = config_.failures_allowed;

  // Set up the command runner if we haven't done so already.
  if (!command_runner_.get()) {
    if (config_.dry_run)
      command_runner_.reset(new DryRunCommandRunner);
    else
      command_runner_.reset(new RealCommandRunner(config_));
  }

  // This main loop runs the entire build process.
  // It is structured like this:
  // First, we attempt to start as many commands as allowed by the
  // command runner.
  // Second, we attempt to wait for / reap the next finished command.
  while (plan_.more_to_do()) {
    // See if we can start any more commands.
    if (failures_allowed && command_runner_->CanRunMore()) {
      if (Edge* edge = plan_.FindWork()) {
        if (!StartEdge(edge, err)) {
          status_->BuildFinished();
          return false;
        }

        if (edge->is_phony()) {
          plan_.EdgeFinished(edge);
        } else {
          ++pending_commands;
        }

        // We made some progress; go back to the main loop.
        continue;
      }
    }

    // See if we can reap any finished commands.
    if (pending_commands) {
      CommandRunner::Result result;
      if (!command_runner_->WaitForCommand(&result) ||
          result.status == ExitInterrupted) {
        status_->BuildFinished();
        *err = "interrupted by user";
        return false;
      }

      --pending_commands;
      if (!FinishCommand(&result, err)) {
        status_->BuildFinished();
        return false;
      }

      if (!result.success()) {
        if (failures_allowed)
          failures_allowed--;
      }

      // We made some progress; start the main loop over.
      continue;
    }

    // If we get here, we cannot make any more progress.
    status_->BuildFinished();
    if (failures_allowed == 0) {
      if (config_.failures_allowed > 1)
        *err = "subcommands failed";
      else
        *err = "subcommand failed";
    } else if (failures_allowed < config_.failures_allowed)
      *err = "cannot make progress due to previous errors";
    else
      *err = "stuck [this is a bug]";

    return false;
  }

  status_->BuildFinished();
  return true;
}

bool Builder::StartEdge(Edge* edge, string* err) {
  METRIC_RECORD("StartEdge");
  if (edge->is_phony())
    return true;

  status_->BuildEdgeStarted(edge);

  // Create directories necessary for outputs.
  // XXX: this will block; do we care?
  for (vector<Node*>::iterator i = edge->outputs_.begin();
       i != edge->outputs_.end(); ++i) {
    if (!disk_interface_->MakeDirs((*i)->path()))
      return false;
  }

  // Create response file, if needed
  // XXX: this may also block; do we care?
  string rspfile = edge->GetBinding("rspfile");
  if (!rspfile.empty()) {
    string content = edge->GetBinding("rspfile_content");
    if (!disk_interface_->WriteFile(rspfile, content))
      return false;
  }

  // start command computing and run it
  if (!command_runner_->StartCommand(edge)) {
    err->assign("command '" + edge->EvaluateCommand() + "' failed.");
    return false;
  }

  return true;
}

bool Builder::FinishCommand(CommandRunner::Result* result, string* err) {
  METRIC_RECORD("FinishCommand");

  Edge* edge = result->edge;

  // First try to extract dependencies from the result, if any.
  // This must happen first as it filters the command output (we want
  // to filter /showIncludes output, even on compile failure) and
  // extraction itself can fail, which makes the command fail from a
  // build perspective.
  vector<Node*> deps_nodes;
  string deps_type = edge->GetBinding("deps");
  if (!deps_type.empty()) {
    string extract_err;
    if (!ExtractDeps(result, deps_type, &deps_nodes, &extract_err) &&
        result->success()) {
      if (!result->output.empty())
        result->output.append("\n");
      result->output.append(extract_err);
      result->status = ExitFailure;
    }
  }

  int start_time, end_time;
  status_->BuildEdgeFinished(edge, result->success(), result->output,
                             &start_time, &end_time);

  // The rest of this function only applies to successful commands.
  if (!result->success())
    return true;

  // Restat the edge outputs, if necessary.
  TimeStamp restat_mtime = 0;
  if (edge->GetBindingBool("restat") && !config_.dry_run) {
    bool node_cleaned = false;

    for (vector<Node*>::iterator i = edge->outputs_.begin();
         i != edge->outputs_.end(); ++i) {
      TimeStamp new_mtime = disk_interface_->Stat((*i)->path());
      if ((*i)->mtime() == new_mtime) {
        // The rule command did not change the output.  Propagate the clean
        // state through the build graph.
        // Note that this also applies to nonexistent outputs (mtime == 0).
        plan_.CleanNode(&scan_, *i);
        node_cleaned = true;
      }
    }

    if (node_cleaned) {
      // If any output was cleaned, find the most recent mtime of any
      // (existing) non-order-only input or the depfile.
      for (vector<Node*>::iterator i = edge->inputs_.begin();
           i != edge->inputs_.end() - edge->order_only_deps_; ++i) {
        TimeStamp input_mtime = disk_interface_->Stat((*i)->path());
        if (input_mtime > restat_mtime)
          restat_mtime = input_mtime;
      }

      string depfile = edge->GetBinding("depfile");
      if (restat_mtime != 0 && deps_type.empty() && !depfile.empty()) {
        TimeStamp depfile_mtime = disk_interface_->Stat(depfile);
        if (depfile_mtime > restat_mtime)
          restat_mtime = depfile_mtime;
      }

      // The total number of edges in the plan may have changed as a result
      // of a restat.
      status_->PlanHasTotalEdges(plan_.command_edge_count());
    }
  }

  plan_.EdgeFinished(edge);

  // Delete any left over response file.
  string rspfile = edge->GetBinding("rspfile");
  if (!rspfile.empty() && !g_keep_rsp)
    disk_interface_->RemoveFile(rspfile);

  if (scan_.build_log()) {
    if (!scan_.build_log()->RecordCommand(edge, start_time, end_time,
                                          restat_mtime)) {
      *err = string("Error writing to build log: ") + strerror(errno);
      return false;
    }
  }

  if (!deps_type.empty() && !config_.dry_run) {
    assert(edge->outputs_.size() == 1 && "should have been rejected by parser");
    Node* out = edge->outputs_[0];
    TimeStamp deps_mtime = disk_interface_->Stat(out->path());
    if (!scan_.deps_log()->RecordDeps(out, deps_mtime, deps_nodes)) {
      *err = string("Error writing to deps log: ") + strerror(errno);
      return false;
    }
  }
  return true;
}

bool Builder::ExtractDeps(CommandRunner::Result* result,
                          const string& deps_type,
                          vector<Node*>* deps_nodes,
                          string* err) {
#ifdef _WIN32
  if (deps_type == "msvc") {
    CLParser parser;
    result->output = parser.Parse(result->output);
    for (set<string>::iterator i = parser.includes_.begin();
         i != parser.includes_.end(); ++i) {
      deps_nodes->push_back(state_->GetNode(*i));
    }
  } else
#endif
  if (deps_type == "gcc") {
    string depfile = result->edge->GetBinding("depfile");
    if (depfile.empty()) {
      *err = string("edge with deps=gcc but no depfile makes no sense");
      return false;
    }

    string content = disk_interface_->ReadFile(depfile, err);
    if (!err->empty())
      return false;
    if (content.empty())
      return true;

    DepfileParser deps;
    if (!deps.Parse(&content, err))
      return false;

    // XXX check depfile matches expected output.
    deps_nodes->reserve(deps.ins_.size());
    for (vector<StringPiece>::iterator i = deps.ins_.begin();
         i != deps.ins_.end(); ++i) {
      if (!CanonicalizePath(const_cast<char*>(i->str_), &i->len_, err))
        return false;
      deps_nodes->push_back(state_->GetNode(*i));
    }

    if (disk_interface_->RemoveFile(depfile) < 0) {
      *err = string("deleting depfile: ") + strerror(errno) + string("\n");
      return false;
    }
  } else {
    Fatal("unknown deps type '%s'", deps_type.c_str());
  }

  return true;
}
