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
#ifndef WIN32
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/termios.h>
#endif

#include "build_log.h"
#include "graph.h"
#include "ninja.h"
#include "subprocess.h"

struct BuildStatus {
  BuildStatus();
  void PlanHasTotalEdges(int total);
  void BuildEdgeStarted(Edge* edge);
  // Returns the time the edge took, in ms.
  int BuildEdgeFinished(Edge* edge);

  void PrintStatus(Edge* edge);

  time_t last_update_;
  int finished_edges_, total_edges_;

  typedef map<Edge*, timeval> RunningEdgeMap;
  RunningEdgeMap running_edges_;

  BuildConfig::Verbosity verbosity_;
  // Whether we can do fancy terminal control codes.
  bool smart_terminal_;
};

BuildStatus::BuildStatus()
    : last_update_(time(NULL)), finished_edges_(0), total_edges_(0),
      verbosity_(BuildConfig::NORMAL) {
  const char* term = getenv("TERM");
  smart_terminal_ = isatty(1) && term && string(term) != "dumb";
}

void BuildStatus::PlanHasTotalEdges(int total) {
  total_edges_ = total;
}

void BuildStatus::BuildEdgeStarted(Edge* edge) {
  timeval now;
  gettimeofday(&now, NULL);
  running_edges_.insert(make_pair(edge, now));

  PrintStatus(edge);
}

int BuildStatus::BuildEdgeFinished(Edge* edge) {
  timeval now;
  gettimeofday(&now, NULL);
  ++finished_edges_;

  if (verbosity_ != BuildConfig::QUIET) {
    if (smart_terminal_ && verbosity_ == BuildConfig::NORMAL) {
      PrintStatus(edge);
      if (finished_edges_ == total_edges_)
        printf("\n");
    } else {
      if (now.tv_sec - last_update_ > 5) {
        printf("%.1f%% %d/%d\n", finished_edges_ * 100 / (float)total_edges_,
               finished_edges_, total_edges_);
        last_update_ = now.tv_sec;
      }
    }
  }

  RunningEdgeMap::iterator i = running_edges_.find(edge);
  timeval delta;
  timersub(&now, &i->second, &delta);
  int ms = (delta.tv_sec * 1000) + (delta.tv_usec / 1000);
  running_edges_.erase(i);

  return ms;
}

void BuildStatus::PrintStatus(Edge* edge) {
  switch (verbosity_) {
  case BuildConfig::QUIET:
    return;

  case BuildConfig::VERBOSE:
    printf("%s\n", edge->EvaluateCommand().c_str());
    break;

  default: {
    string to_print = edge->GetDescription();
    if (to_print.empty() || verbosity_ == BuildConfig::VERBOSE)
      to_print = edge->EvaluateCommand();
#ifndef WIN32
    if (smart_terminal_) {
      // Limit output to width of the terminal so we don't cause line-wrapping.
      winsize size;
      if (ioctl(0, TIOCGWINSZ, &size) == 0) {
        const int kMargin = 15;  // Space for [xxx/yyy] and "...".
        if (to_print.size() + kMargin > size.ws_col) {
          int substr = to_print.size() + kMargin - size.ws_col;
          to_print = "..." + to_print.substr(substr);
        }
      }

      printf("\r[%d/%d] %s\e[K", finished_edges_, total_edges_,
             to_print.c_str());
      fflush(stdout);
    } else
#endif
    {
      printf("%s\n", to_print.c_str());
    }
  }
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
    if (!edge->is_implicit(i - edge->inputs_.begin()) &&
        AddSubTarget(*i, stack, err)) {
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
  virtual bool WaitForCommands();
  virtual Edge* NextFinishedCommand(bool* success);

  const BuildConfig& config_;
  SubprocessSet subprocs_;
  map<Subprocess*, Edge*> subproc_to_edge_;
};

bool RealCommandRunner::CanRunMore() {
  return ((int)subprocs_.running_.size()) < config_.parallelism;
}

bool RealCommandRunner::StartCommand(Edge* edge) {
  string command = edge->EvaluateCommand();
  Subprocess* subproc = new Subprocess(&subprocs_);
  subproc_to_edge_.insert(make_pair(subproc, edge));
  if (!subproc->Start(command))
    return false;

  subprocs_.Add(subproc);
  return true;
}

bool RealCommandRunner::WaitForCommands() {
  if (subprocs_.running_.empty())
    return false;

  while (subprocs_.finished_.empty()) {
    subprocs_.DoWork();
  }
  return true;
}

Edge* RealCommandRunner::NextFinishedCommand(bool* success) {
  Subprocess* subproc = subprocs_.NextFinished();
  if (!subproc)
    return NULL;

  *success = subproc->Finish();

  map<Subprocess*, Edge*>::iterator i = subproc_to_edge_.find(subproc);
  Edge* edge = i->second;
  subproc_to_edge_.erase(i);

  if (!*success ||
      !subproc->stdout_.buf_.empty() ||
      !subproc->stderr_.buf_.empty()) {
    // Print the command that is spewing before printing its output.
    // Print the full command when it failed, otherwise the short name if
    // available.
    string to_print = edge->GetDescription();
    if (to_print.empty() ||
        config_.verbosity == BuildConfig::VERBOSE ||
        !*success) {
      to_print = edge->EvaluateCommand();
    }

    printf("\n%s%s\n", *success ? "" : "FAILED: ", to_print.c_str());
    if (!subproc->stdout_.buf_.empty())
      printf("%s\n", subproc->stdout_.buf_.c_str());
    if (!subproc->stderr_.buf_.empty())
      printf("%s\n", subproc->stderr_.buf_.c_str());
  }

  delete subproc;
  return edge;
}

struct DryRunCommandRunner : public CommandRunner {
  virtual ~DryRunCommandRunner() {}
  virtual bool CanRunMore() {
    return true;
  }
  virtual bool StartCommand(Edge* edge) {
    finished_.push(edge);
    return true;
  }
  virtual bool WaitForCommands() {
    return true;
  }
  virtual Edge* NextFinishedCommand(bool* success) {
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
    : state_(state) {
  disk_interface_ = new RealDiskInterface;
  if (config.dry_run)
    command_runner_ = new DryRunCommandRunner;
  else
    command_runner_ = new RealCommandRunner(config);
  status_ = new BuildStatus;
  status_->verbosity_ = config.verbosity;
  log_ = state->build_log_;
}

Node* Builder::AddTarget(const string& name, string* err) {
  Node* node = state_->LookupNode(name);
  if (!node) {
    *err = "unknown target: '" + name + "'";
    return NULL;
  }
  node->file_->StatIfNecessary(disk_interface_);
  if (node->in_edge_) {
    if (!node->in_edge_->RecomputeDirty(state_, disk_interface_, err))
      return NULL;
  }
  if (!node->dirty_)
    return NULL;  // Intentionally no error.

  if (!plan_.AddTarget(node, err))
    return NULL;
  return node;
}

bool Builder::Build(string* err) {
  if (!plan_.more_to_do()) {
    *err = "no work to do";
    return true;
  }

  status_->PlanHasTotalEdges(plan_.command_edge_count());
  while (plan_.more_to_do()) {
    while (command_runner_->CanRunMore()) {
      Edge* edge = plan_.FindWork();
      if (!edge)
        break;

      if (!StartEdge(edge, err))
        return false;

      if (edge->is_phony())
        FinishEdge(edge);
    }

    if (!plan_.more_to_do())
      break;

    bool success;
    if (Edge* edge = command_runner_->NextFinishedCommand(&success)) {
      if (!success) {
        *err = "subcommand failed";
        return false;
      }
      FinishEdge(edge);
    } else {
      if (!command_runner_->WaitForCommands()) {
        *err = "stuck [this is a bug]";
        return false;
      }
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

void Builder::FinishEdge(Edge* edge) {
  for (vector<Node*>::iterator i = edge->outputs_.begin();
       i != edge->outputs_.end(); ++i) {
    // XXX check that the output actually changed
    // XXX just notify node and have it propagate?
    (*i)->dirty_ = false;
  }
  plan_.EdgeFinished(edge);

  if (edge->is_phony())
    return;

  int ms = status_->BuildEdgeFinished(edge);
  if (log_)
    log_->RecordCommand(edge, ms);
}
