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
#include "ninja.h"
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
        int substr = std::min(to_print.size(),
                              to_print.size() + kMargin - size.ws_col);
        to_print = "..." + to_print.substr(substr);
      }
    }
  }
#endif

  printf("%s", to_print.c_str());

  if (smart_terminal_ && !force_full_command) {
    printf("\e[K");  // Clear to end of line.
    fflush(stdout);
  } else {
    printf("\n");
  }
}

Plan::Plan() : command_edges_(0) {}

bool Plan::AddTarget(Node* node, string* err) {
  vector<Node*> stack;
  return AddSubTarget(node, &stack, err);
}

bool Plan::AddSubTarget(Node* node, vector<Node*>* stack, string* err) {
  Edge* edge = node->in_edge_;
  if (!edge) {  // Leaf node.
    if (node->dirty_) {
      string referenced;
      if (!stack->empty())
        referenced = ", needed by '" + stack->back()->file_->path_ + "',";
      *err = "'" + node->file_->path_ + "'" + referenced + " missing "
             "and no known rule to make it";
    }
    return false;
  }
  assert(edge);

  if (CheckDependencyCycle(node, stack, err))
    return false;

  if (!node->dirty())
    return false;  // Don't need to do anything.
  if (want_.find(edge) != want_.end())
    return true;  // We've already enqueued it.
  want_.insert(edge);
  if (!edge->is_phony())
    ++command_edges_;

  stack->push_back(node);
  bool awaiting_inputs = false;
  for (vector<Node*>::iterator i = edge->inputs_.begin();
       i != edge->inputs_.end(); ++i) {
    if (AddSubTarget(*i, stack, err)) {
      awaiting_inputs = true;
    } else if (!err->empty()) {
      return false;
    }
  }
  assert(stack->back() == node);
  stack->pop_back();

  if (!awaiting_inputs)
    ready_.insert(edge);

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
    err->append((*i)->file_->path_);
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
  want_.erase(edge);

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
    if (want_.find(*i) != want_.end()) {
      // See if the edge is now ready.
      bool ready = true;
      for (vector<Node*>::iterator j = (*i)->inputs_.begin();
           j != (*i)->inputs_.end(); ++j) {
        if ((*j)->dirty()) {
          ready = false;
          break;
        }
      }
      if (ready)
        ready_.insert(*i);
    }
  }
}

void Plan::Dump() {
  printf("pending: %d\n", (int)want_.size());
  for (set<Edge*>::iterator i = want_.begin(); i != want_.end(); ++i) {
    (*i)->Dump();
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
  node->file_->StatIfNecessary(disk_interface_);
  if (node->in_edge_) {
    if (!node->in_edge_->RecomputeDirty(state_, disk_interface_, err))
      return false;
  }
  if (!node->dirty_)
    return false;  // Intentionally no error.

  if (!plan_.AddTarget(node, err))
    return false;

  return true;
}

bool Builder::Build(string* err) {
  if (!plan_.more_to_do()) {
    *err = "no work to do";
    return true;
  }

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
          if (--failures_allowed < 0) {
            if (config_.swallow_failures > 0)
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
    if (!disk_interface_->MakeDirs((*i)->file_->path_))
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
  if (success) {
    for (vector<Node*>::iterator i = edge->outputs_.begin();
         i != edge->outputs_.end(); ++i) {
      (*i)->dirty_ = false;
    }
    plan_.EdgeFinished(edge);
  }

  if (edge->is_phony())
    return;

  int start_time, end_time;
  status_->BuildEdgeFinished(edge, success, output, &start_time, &end_time);
  if (success && log_)
    log_->RecordCommand(edge, start_time, end_time);
}
