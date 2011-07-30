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

  return (int)st.st_mtime;
}

string DirName(const string& path) {

#ifdef WIN32
  const char kPathSeparator = '\\';
#else
  const char kPathSeparator = '/';
#endif
    
  string::size_type slash_pos = path.rfind(kPathSeparator);
  if (slash_pos == string::npos)
    return "";  // Nothing to do.
  while (slash_pos > 0 && path[slash_pos - 1] == kPathSeparator)
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
  if (::MakeDir(path) < 0) {
    Error("mkdir(%s): %s", path.c_str(), strerror(errno));
    return false;
  }
  return true;
}

int RealDiskInterface::RemoveFile(const string& path) {
  if (remove(path.c_str()) < 0) {
    switch (errno) {
      case ENOENT:
        return 1;
      default:
        Error("remove(%s): %s", path.c_str(), strerror(errno));
        return -1;
    }
  } else {
    return 0;
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
    Warning("multiple rules generate %s. "
            "build will not be correct; continuing anyway", path.c_str());
  }
  node->in_edge_ = edge;
}

vector<Node*> State::RootNodes(string* err) {
  vector<Node*> root_nodes;
  // Search for nodes with no output.
  for (vector<Edge*>::iterator e = edges_.begin(); e != edges_.end(); ++e) {
    for (vector<Node*>::iterator out = (*e)->outputs_.begin();
         out != (*e)->outputs_.end(); ++out) {
      if ((*out)->out_edges_.empty())
        root_nodes.push_back(*out);
    }
  }

  if (!edges_.empty() && root_nodes.empty())
    *err = "could not determine root nodes of build graph";

  assert(edges_.empty() || !root_nodes.empty());
  return root_nodes;
}
