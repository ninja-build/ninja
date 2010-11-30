#include "build.h"

#include <stdio.h>

#include "ninja.h"
#include "subprocess.h"

bool Plan::AddTarget(Node* node, string* err) {
  Edge* edge = node->in_edge_;
  if (!edge) {  // Leaf node.
    if (node->dirty_) {
      *err = "'" + node->file_->path_ + "' missing "
             "and no known rule to make it";
    }
    return false;
  }

  assert(edge);
  if (!node->dirty())
    return false;  // Don't need to do anything.
  if (want_.find(edge) != want_.end())
    return true;  // We've already enqueued it.

  bool awaiting_inputs = false;
  for (vector<Node*>::iterator i = edge->inputs_.begin();
       i != edge->inputs_.end(); ++i) {
    if (AddTarget(*i, err))
      awaiting_inputs = true;
    else if (err && !err->empty())
      return false;
  }

  want_.insert(edge);
  if (!awaiting_inputs)
    ready_.insert(edge);

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
  const size_t kConcurrency = 4;  // XXX make configurable etc.
  return subprocs_.running_.size() < kConcurrency;
}

bool RealCommandRunner::StartCommand(Edge* edge) {
  string command = edge->EvaluateCommand();
  printf("  %s\n", command.c_str());
  Subprocess* subproc = new Subprocess;
  subproc_to_edge_.insert(make_pair(subproc, edge));
  if (!subproc->Start(command))
    return false;

  subprocs_.Add(subproc);
  return true;
}

void RealCommandRunner::WaitForCommands() {
  while (subprocs_.finished_.empty()) {
    subprocs_.DoWork();
  }
}

Edge* RealCommandRunner::NextFinishedCommand(bool* success) {
  Subprocess* subproc = subprocs_.NextFinished();
  if (!subproc)
    return NULL;

  *success = subproc->Finish();

  if (!subproc->stdout_.buf_.empty())
    printf("%s\n", subproc->stdout_.buf_.c_str());
  if (!subproc->stderr_.buf_.empty())
    printf("%s\n", subproc->stderr_.buf_.c_str());

  map<Subprocess*, Edge*>::iterator i = subproc_to_edge_.find(subproc);
  Edge* edge = i->second;
  subproc_to_edge_.erase(i);

  delete subproc;
  return edge;
}

Builder::Builder(State* state)
    : state_(state) {
  disk_interface_ = new RealDiskInterface;
  command_runner_ = new RealCommandRunner;
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
}
