// Copyright 2012 Google Inc. All Rights Reserved.
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

#include "deps_log.h"

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "graph.h"
#include "metrics.h"
#include "state.h"
#include "util.h"

bool DepsLog::OpenForWrite(const string& path, string* err) {
  file_ = fopen(path.c_str(), "ab");
  if (!file_) {
    *err = strerror(errno);
    return false;
  }
  SetCloseOnExec(fileno(file_));

  // Opening a file in append mode doesn't set the file pointer to the file's
  // end on Windows. Do that explicitly.
  fseek(file_, 0, SEEK_END);

  /* XXX
  if (ftell(log_file_) == 0) {
    if (fprintf(log_file_, kFileSignature, kCurrentVersion) < 0) {
      *err = strerror(errno);
      return false;
    }
  }
  */

  return true;
}

bool DepsLog::RecordDeps(Node* node, TimeStamp mtime,
                         const vector<Node*>& nodes) {
  // Assign ids to all nodes that are missing one.
  if (node->id() < 0)
    RecordId(node);
  for (vector<Node*>::const_iterator i = nodes.begin();
       i != nodes.end(); ++i) {
    if ((*i)->id() < 0)
      RecordId(*i);
  }

  uint16_t size = 4 * (1 + 1 + (uint16_t)nodes.size());
  size |= 0x8000;  // Deps record: set high bit.
  fwrite(&size, 2, 1, file_);
  int id = node->id();
  fwrite(&id, 4, 1, file_);
  int timestamp = mtime;
  fwrite(&timestamp, 4, 1, file_);
  for (vector<Node*>::const_iterator i = nodes.begin();
       i != nodes.end(); ++i) {
    id = (*i)->id();
    fwrite(&id, 4, 1, file_);
  }

  return true;
}

void DepsLog::Close() {
  fclose(file_);
  file_ = NULL;
}

bool DepsLog::Load(const string& path, State* state, string* err) {
  METRIC_RECORD(".ninja_deps load");
  char buf[32 << 10];
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    if (errno == ENOENT)
      return true;
    *err = strerror(errno);
    return false;
  }

  for (;;) {
    uint16_t size;
    if (fread(&size, 2, 1, f) < 1)
      break;
    bool is_deps = (size >> 15) != 0;
    size = size & 0x7FFF;

    if (fread(buf, size, 1, f) < 1)
      break;

    if (is_deps) {
      assert(size % 4 == 0);
      int* deps_data = reinterpret_cast<int*>(buf);
      int out_id = deps_data[0];
      int mtime = deps_data[1];
      deps_data += 2;
      int deps_count = (size / 4) - 2;

      Deps* deps = new Deps;
      deps->mtime = mtime;
      deps->node_count = deps_count;
      deps->nodes = new Node*[deps_count];
      for (int i = 0; i < deps_count; ++i) {
        assert(deps_data[i] < (int)nodes_.size());
        assert(nodes_[deps_data[i]]);
        deps->nodes[i] = nodes_[deps_data[i]];
      }

      if (out_id >= (int)deps_.size())
        deps_.resize(out_id + 1);
      if (deps_[out_id])
        delete deps_[out_id];
      deps_[out_id] = deps;
    } else {
      StringPiece path(buf, size);
      Node* node = state->GetNode(path);
      assert(node->id() < 0);
      node->set_id(nodes_.size());
      nodes_.push_back(node);
    }
  }
  if (ferror(f)) {
    *err = strerror(ferror(f));
    return false;
  }
  fclose(f);
  return true;
}

DepsLog::Deps* DepsLog::GetDeps(Node* node) {
  if (node->id() < 0)
    return NULL;
  return deps_[node->id()];
}

bool DepsLog::RecordId(Node* node) {
  uint16_t size = (uint16_t)node->path().size();
  fwrite(&size, 2, 1, file_);
  fwrite(node->path().data(), node->path().size(), 1, file_);

  node->set_id(nodes_.size());
  nodes_.push_back(node);

  return true;
}
