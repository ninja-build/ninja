#include "build.h"

#include <stdio.h>

#include "ninja.h"
#include "subprocess.h"

struct BuildStatusLog {
  BuildStatusLog();
  virtual void PlanHasTotalEdges(int total);
  virtual void BuildEdgeStarted(Edge* edge);
  virtual void BuildEdgeFinished(Edge* edge);

  time_t last_update_;
  int finished_edges_, total_edges_;
  bool verbose_;
};

BuildStatusLog::BuildStatusLog()
    : last_update_(time(NULL)), finished_edges_(0), total_edges_(0),
      verbose_(false) {}

void BuildStatusLog::PlanHasTotalEdges(int total) {
  total_edges_ = total;
}

void BuildStatusLog::BuildEdgeStarted(Edge* edge) {
  string desc = edge->GetDescription();
  if (!verbose_ && !desc.empty())
    printf("%s\n", desc.c_str());
  else
    printf("%s\n", edge->EvaluateCommand().c_str());
}

void BuildStatusLog::BuildEdgeFinished(Edge* edge) {
  ++finished_edges_;
  time_t now = time(NULL);
  if (now - last_update_ > 5) {
    printf("%.1f%% %d/%d\n", finished_edges_ * 100 / (float)total_edges_,
           finished_edges_, total_edges_);
    last_update_ = now;
  }
}

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
  virtual ~RealCommandRunner() {}
  virtual bool CanRunMore();
  virtual bool StartCommand(Edge* edge);
  virtual void WaitForCommands();
  virtual Edge* NextFinishedCommand(bool* success);

  SubprocessSet subprocs_;
  map<Subprocess*, Edge*> subproc_to_edge_;
};

bool RealCommandRunner::CanRunMore() {
  const size_t kConcurrency = 8;  // XXX make configurable etc.
  return subprocs_.running_.size() < kConcurrency;
}

bool RealCommandRunner::StartCommand(Edge* edge) {
  string command = edge->EvaluateCommand();
  Subprocess* subproc = new Subprocess;
  subproc_to_edge_.insert(make_pair(subproc, edge));
  if (!subproc->Start(command))
    return false;

  subprocs_.Add(subproc);
  return true;
}

void RealCommandRunner::WaitForCommands() {
  while (subprocs_.finished_.empty() && !subprocs_.running_.empty()) {
    subprocs_.DoWork();
  }
}

Edge* RealCommandRunner::NextFinishedCommand(bool* success) {
  Subprocess* subproc = subprocs_.NextFinished();
  if (!subproc)
    return NULL;

  *success = subproc->Finish();

  map<Subprocess*, Edge*>::iterator i = subproc_to_edge_.find(subproc);
  Edge* edge = i->second;
  subproc_to_edge_.erase(i);

  if (!*success)
    printf("FAILED: %s\n", edge->EvaluateCommand().c_str());
  if (!subproc->stdout_.buf_.empty())
    printf("%s\n", subproc->stdout_.buf_.c_str());
  if (!subproc->stderr_.buf_.empty())
    printf("%s\n", subproc->stderr_.buf_.c_str());

  delete subproc;
  return edge;
}

Builder::Builder(State* state)
    : state_(state) {
  disk_interface_ = new RealDiskInterface;
  command_runner_ = new RealCommandRunner;
  log_ = new BuildStatusLog;
}

void Builder::SetVerbose(bool verbose) {
  log_->verbose_ = verbose;
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

  log_->PlanHasTotalEdges(plan_.edge_count());
  while (plan_.more_to_do()) {
    while (command_runner_->CanRunMore()) {
      Edge* edge = plan_.FindWork();
      if (!edge)
        break;

      if (edge->rule_ == &State::kPhonyRule) {
        FinishEdge(edge);
        continue;
      }

      if (!StartEdge(edge, err))
        return false;
      log_->BuildEdgeStarted(edge);
    }

    bool success;
    if (Edge* edge = command_runner_->NextFinishedCommand(&success)) {
      if (!success) {
        *err = "subcommand failed";
        return false;
      }
      FinishEdge(edge);
    } else {
      command_runner_->WaitForCommands();
    }
  }

  return true;
}

bool Builder::StartEdge(Edge* edge, string* err) {
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
  log_->BuildEdgeFinished(edge);
}
