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

// This file is all the code that used to be in one file.
// TODO: split into modules, delete this file.

#include "ninja.h"

#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

#include "build_log.h"
#include "graph.h"
#include "util.h"

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
      Error("stat(%s): %s", path.c_str(), strerror(errno));
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
    Error("mkdir(%s): %s", path.c_str(), strerror(errno));
    return false;
  }
  return true;
}

FileStat* StatCache::GetFile(const string& path) {
  Paths::iterator i = paths_.find(path);
  if (i != paths_.end())
    return i->second;
  FileStat* file = new FileStat(path);
  paths_[path] = file;
  return file;
}

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

const Rule State::kPhonyRule("phony");

State::State() : build_log_(NULL) {
  AddRule(&kPhonyRule);
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

void State::AddIn(Edge* edge, const string& path) {
  Node* node = GetNode(path);
  edge->inputs_.push_back(node);
  node->out_edges_.push_back(edge);
}

void State::AddOut(Edge* edge, const string& path) {
  Node* node = GetNode(path);
  edge->outputs_.push_back(node);
  if (node->in_edge_) {
    Error("WARNING: multiple rules generate %s. "
          "build will not be correct; continuing anyway", path.c_str());
  }
  node->in_edge_ = edge;
}

vector<Node*> State::RootNodes()
{
  vector<Node*> root_nodes;
  // Search for nodes with no output.
  for (vector<Edge*>::iterator e = edges_.begin(); e != edges_.end(); ++e)
    for (vector<Node*>::iterator outs = (*e)->outputs_.begin();
         outs != (*e)->outputs_.end();
         ++outs)
      if ((*outs)->out_edges_.size() == 0)
        root_nodes.push_back(*outs);
  assert(edges_.empty() || !root_nodes.empty());
  return root_nodes;
}
