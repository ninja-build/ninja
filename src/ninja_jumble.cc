// This file is all the code that used to be in one file.
// TODO: split into modules, delete this file.

#include "ninja.h"

#include "build_log.h"

#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

int ReadFile(const string& path, string* contents, string* err) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) {
    err->assign(strerror(errno));
    return -errno;
  }

  char buf[64 << 10];
  size_t len;
  while ((len = fread(buf, 1, sizeof(buf), f)) > 0) {
    contents->append(buf, len);
  }
  if (ferror(f)) {
    err->assign(strerror(errno));  // XXX errno?
    contents->clear();
    fclose(f);
    return -errno;
  }
  fclose(f);
  return 0;
}

int RealDiskInterface::Stat(const string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) < 0) {
    if (errno == ENOENT) {
      return 0;
    } else {
      fprintf(stderr, "stat(%s): %s\n", path.c_str(), strerror(errno));
      return -1;
    }
  }

  return st.st_mtime;
  return true;
}

string DirName(const string& path) {
  string::size_type slash_pos = path.rfind('/');
  if (slash_pos == string::npos)
    return "";  // Nothing to do.
  while (slash_pos > 0 && path[slash_pos - 1] == '/')
    --slash_pos;
  return path.substr(0, slash_pos);
}

bool DiskInterface::MakeDirs(const string& path) {
  string dir = DirName(path);
  if (dir.empty())
    return true;  // Reached root; assume it's there.
  int mtime = Stat(dir);
  if (mtime < 0)
    return false;  // Error.
  if (mtime > 0)
    return true;  // Exists already; we're done.

  // Directory doesn't exist.  Try creating its parent first.
  bool success = MakeDirs(dir);
  if (!success)
    return false;
  return MakeDir(dir);
}

string RealDiskInterface::ReadFile(const string& path, string* err) {
  string contents;
  int ret = ::ReadFile(path, &contents, err);
  if (ret == -ENOENT) {
    // Swallow ENOENT.
    err->clear();
  }
  return contents;
}

bool RealDiskInterface::MakeDir(const string& path) {
  if (mkdir(path.c_str(), 0777) < 0) {
    fprintf(stderr, "mkdir(%s): %s\n", path.c_str(), strerror(errno));
    return false;
  }
  return true;
}
void FileStat::Touch(int mtime) {
  mtime_ = mtime;
  if (node_)
    node_->MarkDirty();
}

bool FileStat::Stat(DiskInterface* disk_interface) {
  mtime_ = disk_interface->Stat(path_);
  return mtime_ > 0;
}

void Node::MarkDirty() {
  if (dirty_)
    return;  // We already know.

  dirty_ = true;
  MarkDependentsDirty();
}

void Node::MarkDependentsDirty() {
  for (vector<Edge*>::iterator i = out_edges_.begin(); i != out_edges_.end(); ++i)
    (*i)->MarkDirty(this);
}

bool Edge::RecomputeDirty(State* state, DiskInterface* disk_interface,
                          string* err) {
  bool dirty = false;

  if (!rule_->depfile_.empty()) {
    if (!LoadDepFile(state, disk_interface, err))
      return false;
  }

  time_t most_recent_input = 1;
  for (vector<Node*>::iterator i = inputs_.begin(); i != inputs_.end(); ++i) {
    if ((*i)->file_->StatIfNecessary(disk_interface)) {
      if (Edge* edge = (*i)->in_edge_) {
        if (!edge->RecomputeDirty(state, disk_interface, err))
          return false;
      } else {
        (*i)->dirty_ = !(*i)->file_->exists();
      }
    }

    if (is_order_only(i - inputs_.begin())) {
      // Order-only deps only make us dirty if they're missing.
      if (!(*i)->file_->exists())
        dirty = true;
      continue;
    }

    // If a regular input is dirty (or missing), we're dirty.
    // Otherwise consider mtime.
    if ((*i)->dirty_) {
      dirty = true;
    } else {
      if ((*i)->file_->mtime_ > most_recent_input)
        most_recent_input = (*i)->file_->mtime_;
    }
  }

  assert(!outputs_.empty());
  for (vector<Node*>::iterator i = outputs_.begin(); i != outputs_.end(); ++i) {
    // We may have other outputs, that our input-recursive traversal hasn't hit
    // yet (or never will).  Stat them if we haven't already.
    (*i)->file_->StatIfNecessary(disk_interface);

    // Output is dirty if we're dirty, we're missing the output,
    // or if it's older than the mostt recent input mtime.
    if (dirty || !(*i)->file_->exists() ||
        (*i)->file_->mtime_ < most_recent_input) {
      (*i)->dirty_ = true;
    }
  }
  return true;
}

void Edge::MarkDirty(Node* node) {
  if (rule_ == &State::kPhonyRule)
    return;

  vector<Node*>::iterator i = find(inputs_.begin(), inputs_.end(), node);
  if (i == inputs_.end())
    return;
  if (i - inputs_.begin() >= ((int)inputs_.size()) - order_only_deps_)
    return;  // Order-only deps don't cause us to become dirty.
  for (i = outputs_.begin(); i != outputs_.end(); ++i)
    (*i)->MarkDirty();
}

struct EdgeEnv : public Env {
  EdgeEnv(Edge* edge) : edge_(edge) {}
  virtual string LookupVariable(const string& var) {
    string result;
    if (var == "in") {
      int explicit_deps = edge_->inputs_.size() - edge_->implicit_deps_ -
          edge_->order_only_deps_;
      for (vector<Node*>::iterator i = edge_->inputs_.begin();
           i != edge_->inputs_.end() && explicit_deps; ++i, --explicit_deps) {
        if (!result.empty())
          result.push_back(' ');
        result.append((*i)->file_->path_);
      }
    } else if (var == "out") {
      result = edge_->outputs_[0]->file_->path_;
    } else if (edge_->env_) {
      return edge_->env_->LookupVariable(var);
    }
    return result;
  }
  Edge* edge_;
};

string Edge::EvaluateCommand() {
  EdgeEnv env(this);
  return rule_->command_.Evaluate(&env);
}

string Edge::GetDescription() {
  EdgeEnv env(this);
  return rule_->description_.Evaluate(&env);
}

FileStat* StatCache::GetFile(const string& path) {
  Paths::iterator i = paths_.find(path);
  if (i != paths_.end())
    return i->second;
  FileStat* file = new FileStat(path);
  paths_[path] = file;
  return file;
}

#include <stdio.h>

void StatCache::Dump() {
  for (Paths::iterator i = paths_.begin(); i != paths_.end(); ++i) {
    FileStat* file = i->second;
    printf("%s %s\n",
           file->path_.c_str(),
           file->status_known()
           ? (file->node_->dirty_ ? "dirty" : "clean")
           : "unknown");
  }
}

#include "parsers.h"

bool Edge::LoadDepFile(State* state, DiskInterface* disk_interface, string* err) {
  EdgeEnv env(this);
  string path = rule_->depfile_.Evaluate(&env);

  string content = disk_interface->ReadFile(path, err);
  if (!err->empty())
    return false;
  if (content.empty())
    return true;

  MakefileParser makefile;
  if (!makefile.Parse(content, err))
    return false;

  // Check that this depfile matches our output.
  if (outputs_.size() != 1) {
    *err = "expected only one output";
    return false;
  }
  if (outputs_[0]->file_->path_ != makefile.out_) {
    *err = "expected makefile to mention '" + outputs_[0]->file_->path_ + "', "
           "got '" + makefile.out_ + "'";
    return false;
  }

  // Add all its in-edges.
  for (vector<string>::iterator i = makefile.ins_.begin();
       i != makefile.ins_.end(); ++i) {
    Node* node = state->GetNode(*i);
    for (vector<Node*>::iterator j = inputs_.begin(); j != inputs_.end(); ++j) {
      if (*j == node) {
        node = NULL;
        break;
      }
    }
    if (node) {
      inputs_.insert(inputs_.end() - order_only_deps_, node);
      node->out_edges_.push_back(this);
      ++implicit_deps_;
    }
  }

  return true;
}

void Edge::Dump() {
  printf("[ ");
  for (vector<Node*>::iterator i = inputs_.begin(); i != inputs_.end(); ++i) {
    printf("%s ", (*i)->file_->path_.c_str());
  }
  printf("--%s-> ", rule_->name_.c_str());
  for (vector<Node*>::iterator i = outputs_.begin(); i != outputs_.end(); ++i) {
    printf("%s ", (*i)->file_->path_.c_str());
  }
  printf("]\n");
}

const Rule State::kPhonyRule("phony");

State::State() : build_log_(NULL) {
  AddRule(&kPhonyRule);
  build_log_ = new BuildLog;
}

const Rule* State::LookupRule(const string& rule_name) {
  map<string, const Rule*>::iterator i = rules_.find(rule_name);
  if (i == rules_.end())
    return NULL;
  return i->second;
}

void State::AddRule(const Rule* rule) {
  assert(LookupRule(rule->name_) == NULL);
  rules_[rule->name_] = rule;
}

Edge* State::AddEdge(const Rule* rule) {
  Edge* edge = new Edge();
  edge->rule_ = rule;
  edge->env_ = &bindings_;
  edges_.push_back(edge);
  return edge;
}

Node* State::LookupNode(const string& path) {
  FileStat* file = stat_cache_.GetFile(path);
  if (!file->node_)
    return NULL;
  return file->node_;
}

Node* State::GetNode(const string& path) {
  FileStat* file = stat_cache_.GetFile(path);
  if (!file->node_)
    file->node_ = new Node(file);
  return file->node_;
}

void State::AddInOut(Edge* edge, Edge::InOut inout, const string& path) {
  Node* node = GetNode(path);
  if (inout == Edge::IN) {
    edge->inputs_.push_back(node);
    node->out_edges_.push_back(edge);
  } else {
    edge->outputs_.push_back(node);
    if (node->in_edge_) {
      fprintf(stderr, "WARNING: multiple rules generate %s. "
              "build will not be correct; continuing anyway\n", path.c_str());
    }
    node->in_edge_ = edge;
  }
}

bool EvalString::Parse(const string& input, string* err) {
  unparsed_ = input;

  string::size_type start, end;
  start = 0;
  do {
    end = input.find('$', start);
    if (end == string::npos) {
      end = input.size();
      break;
    }
    if (end > start)
      parsed_.push_back(make_pair(input.substr(start, end - start), RAW));
    start = end + 1;
    if (start < input.size() && input[start] == '{') {
      ++start;
      for (end = start + 1; end < input.size(); ++end) {
        if (input[end] == '}')
          break;
      }
      if (end >= input.size()) {
        *err = "expected closing curly after ${";
        return false;
      }
      parsed_.push_back(make_pair(input.substr(start, end - start), SPECIAL));
      ++end;
    } else {
      for (end = start; end < input.size(); ++end) {
        char c = input[end];
        if (!(('a' <= c && c <= 'z') || ('0' <= c && c <= '9') || c == '_'))
          break;
      }
      if (end == start) {
        *err = "expected variable after $";
        return false;
      }
      parsed_.push_back(make_pair(input.substr(start, end - start), SPECIAL));
    }
    start = end;
  } while (end < input.size());
  if (end > start)
    parsed_.push_back(make_pair(input.substr(start, end - start), RAW));

  return true;
}

string EvalString::Evaluate(Env* env) const {
  string result;
  for (TokenList::const_iterator i = parsed_.begin(); i != parsed_.end(); ++i) {
    if (i->second == RAW)
      result.append(i->first);
    else
      result.append(env->LookupVariable(i->first));
  }
  return result;
}
