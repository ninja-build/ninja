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
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/termios.h>
#endif

#include "build_log.h"
#include "disk_interface.h"
#include "graph.h"
#include "state.h"
#include "subprocess.h"
#include "util.h"

/// Tracks the status of a build: completion fraction, printing updates.
struct BuildStatus {
  BuildStatus(const BuildConfig& config);
  void PlanHasTotalEdges(int total);
  void BuildEdgeStarted(Edge* edge);
  void BuildEdgeFinished(Edge* edge, bool success, const string& output,
                         int* start_time, int* end_time);

 private:
  void PrintStatus(Edge* edge);

  const BuildConfig& config_;

  /// Time the build started.
  int64_t start_time_millis_;
  /// Time we last printed an update.
  int64_t last_update_millis_;

  int started_edges_, finished_edges_, total_edges_;

  /// Map of running edge to time the edge started running.
  typedef map<Edge*, int> RunningEdgeMap;
  RunningEdgeMap running_edges_;

  /// Whether we can do fancy terminal control codes.
  bool smart_terminal_;
};

BuildStatus::BuildStatus(const BuildConfig& config)
    : config_(config),
      start_time_millis_(GetTimeMillis()),
      last_update_millis_(start_time_millis_),
      started_edges_(0), finished_edges_(0), total_edges_(0) {
#ifndef WIN32
  const char* term = getenv("TERM");
  smart_terminal_ = isatty(1) && term && string(term) != "dumb";
#else
  smart_terminal_ = false;
  // Disable output buffer.  It'd be nice to use line buffering but
  // MSDN says: "For some systems, [_IOLBF] provides line
  // buffering. However, for Win32, the behavior is the same as _IOFBF
  // - Full Buffering."
  setvbuf(stdout, NULL, _IONBF, 0);
#endif

  // Don't do anything fancy in verbose mode.
  if (config_.verbosity != BuildConfig::NORMAL)
    smart_terminal_ = false;
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
  int total_time = end_time - start_time;
  running_edges_.erase(i);

  if (config_.verbosity == BuildConfig::QUIET)
    return;

  if (smart_terminal_)
    PrintStatus(edge);

  if (success && output.empty()) {
    if (smart_terminal_) {
      if (finished_edges_ == total_edges_)
        printf("\n");
    } else {
      if (total_time > 5*1000) {
        printf("%.1f%% %d/%d\n", finished_edges_ * 100 / (float)total_edges_,
               finished_edges_, total_edges_);
        last_update_millis_ = now;
      }
    }
  } else {
    if (smart_terminal_)
      printf("\n");

    // Print the command that is spewing before printing its output.
    if (!success)
      printf("FAILED: %s\n", edge->EvaluateCommand().c_str());

    if (!output.empty())
      printf("%s", output.c_str());
  }
}

void BuildStatus::PrintStatus(Edge* edge) {
  if (config_.verbosity == BuildConfig::QUIET)
    return;

  bool force_full_command = config_.verbosity == BuildConfig::VERBOSE;

  string to_print = edge->GetDescription();
  if (to_print.empty() || force_full_command)
    to_print = edge->EvaluateCommand();

  if (smart_terminal_)
    printf("\r");  // Print over previous line, if any.

  int progress_chars = printf("[%d/%d] ", started_edges_, total_edges_);

#ifndef WIN32
  if (smart_terminal_ && !force_full_command) {
    // Limit output to width of the terminal if provided so we don't cause
    // line-wrapping.
    winsize size;
    if ((ioctl(0, TIOCGWINSZ, &size) == 0) && size.ws_col) {
      const int kMargin = progress_chars + 3;  // Space for [xx/yy] and "...".
      if (to_print.size() + kMargin > size.ws_col) {
        int elide_size = (size.ws_col - kMargin) / 2;
        to_print = to_print.substr(0, elide_size)
          + "..."
          + to_print.substr(to_print.size() - elide_size, elide_size);
      }
    }
  }
#else
  NINJA_UNUSED_ARG(progress_chars);
#endif

  printf("%s", to_print.c_str());

  if (smart_terminal_ && !force_full_command) {
    printf("\e[K");  // Clear to end of line.
    fflush(stdout);
  } else {
    printf("\n");
  }
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
      ready_.insert(edge);
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

void Plan::EdgeFinished(Edge* edge) {
  map<Edge*, bool>::iterator i = want_.find(edge);
  assert(i != want_.end());
  if (i->second)
    --wanted_edges_;
  want_.erase(i);
  edge->outputs_ready_ = true;

  // Check off any nodes we were waiting for with this edge.
  for (vector<Node*>::iterator i = edge->outputs_.begin();
       i != edge->outputs_.end(); ++i) {
    NodeFinished(*i);
  }
}

void Plan::NodeFinished(Node* node) {
  // See if we we want any edges from this node.
  for (vector<Edge*>::iterator i = node->out_edges_.begin();
       i != node->out_edges_.end(); ++i) {
    map<Edge*, bool>::iterator want_i = want_.find(*i);
    if (want_i == want_.end())
      continue;

    // See if the edge is now ready.
    if ((*i)->AllInputsReady()) {
      if (want_i->second) {
        ready_.insert(*i);
      } else {
        // We do not need to build this edge, but we might need to build one of
        // its dependents.
        EdgeFinished(*i);
      }
    }
  }
}

void Plan::CleanNode(BuildLog* build_log, Node* node) {
  node->set_dirty(false);

  for (vector<Edge*>::iterator ei = node->out_edges_.begin();
       ei != node->out_edges_.end(); ++ei) {
    // Don't process edges that we don't actually want.
    map<Edge*, bool>::iterator want_i = want_.find(*ei);
    if (want_i == want_.end() || !want_i->second)
      continue;

    // If all non-order-only inputs for this edge are now clean,
    // we might have changed the dirty state of the outputs.
    vector<Node*>::iterator begin = (*ei)->inputs_.begin(),
                            end = (*ei)->inputs_.end() - (*ei)->order_only_deps_;
    if (find_if(begin, end, mem_fun(&Node::dirty)) == end) {
      // Recompute most_recent_input and command.
      time_t most_recent_input = 1;
      for (vector<Node*>::iterator ni = begin; ni != end; ++ni)
        if ((*ni)->mtime() > most_recent_input)
          most_recent_input = (*ni)->mtime();
      string command = (*ei)->EvaluateCommand();

      // Now, recompute the dirty state of each output.
      bool all_outputs_clean = true;
      for (vector<Node*>::iterator ni = (*ei)->outputs_.begin();
           ni != (*ei)->outputs_.end(); ++ni) {
        if (!(*ni)->dirty())
          continue;

        if ((*ei)->RecomputeOutputDirty(build_log, most_recent_input, command,
                                        *ni)) {
          (*ni)->MarkDirty();
          all_outputs_clean = false;
        } else {
          CleanNode(build_log, *ni);
        }
      }

      // If we cleaned all outputs, mark the node as not wanted.
      if (all_outputs_clean) {
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
  RealCommandRunner(const BuildConfig& config) : config_(config) {}
  virtual ~RealCommandRunner() {}
  virtual bool CanRunMore();
  virtual bool StartCommand(Edge* edge);
  virtual Edge* WaitForCommand(bool* success, string* output);

  const BuildConfig& config_;
  SubprocessSet subprocs_;
  map<Subprocess*, Edge*> subproc_to_edge_;
};

bool RealCommandRunner::CanRunMore() {
  return ((int)subprocs_.running_.size()) < config_.parallelism;
}

bool RealCommandRunner::StartCommand(Edge* edge) {
  string command = edge->EvaluateCommand();
  Subprocess* subproc = new Subprocess;
  subproc_to_edge_.insert(make_pair(subproc, edge));
  if (!subproc->Start(&subprocs_, command))
    return false;

  subprocs_.Add(subproc);
  return true;
}

Edge* RealCommandRunner::WaitForCommand(bool* success, string* output) {
  Subprocess* subproc;
  while ((subproc = subprocs_.NextFinished()) == NULL) {
    subprocs_.DoWork();
  }

  *success = subproc->Finish();
  *output = subproc->GetOutput();

  map<Subprocess*, Edge*>::iterator i = subproc_to_edge_.find(subproc);
  Edge* edge = i->second;
  subproc_to_edge_.erase(i);

  delete subproc;
  return edge;
}

/// A CommandRunner that doesn't actually run the commands.
struct DryRunCommandRunner : public CommandRunner {
  virtual ~DryRunCommandRunner() {}
  virtual bool CanRunMore() {
    return true;
  }
  virtual bool StartCommand(Edge* edge) {
    finished_.push(edge);
    return true;
  }
  virtual Edge* WaitForCommand(bool* success, string* output) {
    if (finished_.empty())
      return NULL;
    *success = true;
    Edge* edge = finished_.front();
    finished_.pop();
    return edge;
  }

  queue<Edge*> finished_;
};

Builder::Builder(State* state, const BuildConfig& config)
    : state_(state), config_(config) {
  disk_interface_ = new RealDiskInterface;
  if (config.dry_run)
    command_runner_ = new DryRunCommandRunner;
  else
    command_runner_ = new RealCommandRunner(config);
  status_ = new BuildStatus(config);
  log_ = state->build_log_;
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
    if (!in_edge->RecomputeDirty(state_, disk_interface_, err))
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
  int failures_allowed = config_.swallow_failures;

  // This main loop runs the entire build process.
  // It is structured like this:
  // First, we attempt to start as many commands as allowed by the
  // command runner.
  // Second, we attempt to wait for / reap the next finished command.
  // If we can do neither of those, the build is stuck, and we report
  // an error.
  while (plan_.more_to_do()) {
    // See if we can start any more commands.
    if (command_runner_->CanRunMore()) {
      if (Edge* edge = plan_.FindWork()) {
        if (!StartEdge(edge, err))
          return false;

        if (edge->is_phony())
          FinishEdge(edge, true, "");
        else
          ++pending_commands;

        // We made some progress; go back to the main loop.
        continue;
      }
    }

    // See if we can reap any finished commands.
    if (pending_commands) {
      bool success;
      string output;
      Edge* edge;
      if ((edge = command_runner_->WaitForCommand(&success, &output))) {
        --pending_commands;
        FinishEdge(edge, success, output);
        if (!success) {
          if (failures_allowed-- == 0) {
            if (config_.swallow_failures != 0)
              *err = "subcommands failed";
            else
              *err = "subcommand failed";
            return false;
          }
        }

        // We made some progress; start the main loop over.
        continue;
      }
    }

    // If we get here, we can neither enqueue new commands nor are any running.
    if (pending_commands) {
      *err = "stuck: pending commands but none to wait for? [this is a bug]";
      return false;
    }

    // If we get here, we cannot make any more progress.
    if (failures_allowed < config_.swallow_failures) {
      *err = "cannot make progress due to previous errors";
      return false;
    } else {
      *err = "stuck [this is a bug]";
      return false;
    }
  }

  return true;
}

bool Builder::StartEdge(Edge* edge, string* err) {
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

  // Compute command and start it.
  string command = edge->EvaluateCommand();
  if (!command_runner_->StartCommand(edge)) {
    err->assign("command '" + command + "' failed.");
    return false;
  }

  return true;
}

void Builder::FinishEdge(Edge* edge, bool success, const string& output) {
  time_t restat_mtime = 0;

  if (success) {
    if (edge->rule().restat_) {
      bool node_cleaned = false;

      for (vector<Node*>::iterator i = edge->outputs_.begin();
           i != edge->outputs_.end(); ++i) {
        if ((*i)->exists()) {
          time_t new_mtime = disk_interface_->Stat((*i)->path());
          if ((*i)->mtime() == new_mtime) {
            // The rule command did not change the output.  Propagate the clean
            // state through the build graph.
            plan_.CleanNode(log_, *i);
            node_cleaned = true;
          }
        }
      }

      if (node_cleaned) {
        // If any output was cleaned, find the most recent mtime of any
        // (existing) non-order-only input or the depfile.
        for (vector<Node*>::iterator i = edge->inputs_.begin();
             i != edge->inputs_.end() - edge->order_only_deps_; ++i) {
          time_t input_mtime = disk_interface_->Stat((*i)->path());
          if (input_mtime == 0) {
            restat_mtime = 0;
            break;
          }
          if (input_mtime > restat_mtime)
            restat_mtime = input_mtime;
        }

        if (restat_mtime != 0 && !edge->rule().depfile_.empty()) {
          time_t depfile_mtime = disk_interface_->Stat(edge->EvaluateDepFile());
          if (depfile_mtime == 0)
            restat_mtime = 0;
          else if (depfile_mtime > restat_mtime)
            restat_mtime = depfile_mtime;
        }

        // The total number of edges in the plan may have changed as a result
        // of a restat.
        status_->PlanHasTotalEdges(plan_.command_edge_count());
      }
    }

    plan_.EdgeFinished(edge);
  }

  if (edge->is_phony())
    return;

  int start_time, end_time;
  status_->BuildEdgeFinished(edge, success, output, &start_time, &end_time);
  if (success && log_)
    log_->RecordCommand(edge, start_time, end_time, restat_mtime);
}
