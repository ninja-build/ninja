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
#ifndef _WIN32
#include <unistd.h>
#endif

#include "graph.h"
#include "metrics.h"
#include "state.h"
#include "util.h"

// The version is stored as 4 bytes after the signature and also serves as a
// byte order mark. Signature and version combined are 16 bytes long.
const char kFileSignature[] = "# ninjadeps\n";
const int kCurrentVersion = 1;

DepsLog::~DepsLog() {
  Close();
}

bool DepsLog::OpenForWrite(const string& path, string* err) {
  if (needs_recompaction_) {
    Close();
    if (!Recompact(path, err))
      return false;
  }
  
  file_ = fopen(path.c_str(), "ab");
  if (!file_) {
    *err = strerror(errno);
    return false;
  }
  SetCloseOnExec(fileno(file_));

  // Opening a file in append mode doesn't set the file pointer to the file's
  // end on Windows. Do that explicitly.
  fseek(file_, 0, SEEK_END);

  if (ftell(file_) == 0) {
    if (fwrite(kFileSignature, sizeof(kFileSignature) - 1, 1, file_) < 1) {
      *err = strerror(errno);
      return false;
    }
    if (fwrite(&kCurrentVersion, 4, 1, file_) < 1) {
      *err = strerror(errno);
      return false;
    }
  }

  return true;
}

bool DepsLog::RecordDeps(Node* node, TimeStamp mtime,
                         const vector<Node*>& nodes) {
  return RecordDeps(node, mtime, nodes.size(),
                    nodes.empty() ? NULL : (Node**)&nodes.front());
}

bool DepsLog::RecordDeps(Node* node, TimeStamp mtime,
                         int node_count, Node** nodes) {
  // Track whether there's any new data to be recorded.
  bool made_change = false;

  // Assign ids to all nodes that are missing one.
  if (node->id() < 0) {
    RecordId(node);
    made_change = true;
  }
  for (int i = 0; i < node_count; ++i) {
    if (nodes[i]->id() < 0) {
      RecordId(nodes[i]);
      made_change = true;
    }
  }

  // See if the new data is different than the existing data, if any.
  if (!made_change) {
    Deps* deps = GetDeps(node);
    if (!deps ||
        deps->mtime != mtime ||
        deps->node_count != node_count) {
      made_change = true;
    } else {
      for (int i = 0; i < node_count; ++i) {
        if (deps->nodes[i] != nodes[i]) {
          made_change = true;
          break;
        }
      }
    }
  }

  // Don't write anything if there's no new info.
  if (!made_change)
    return true;

  // Update on-disk representation.
  uint16_t size = 4 * (1 + 1 + (uint16_t)node_count);
  size |= 0x8000;  // Deps record: set high bit.
  fwrite(&size, 2, 1, file_);
  int id = node->id();
  fwrite(&id, 4, 1, file_);
  int timestamp = mtime;
  fwrite(&timestamp, 4, 1, file_);
  for (int i = 0; i < node_count; ++i) {
    id = nodes[i]->id();
    fwrite(&id, 4, 1, file_);
  }

  // Update in-memory representation.
  Deps* deps = new Deps(mtime, node_count);
  for (int i = 0; i < node_count; ++i)
    deps->nodes[i] = nodes[i];
  UpdateDeps(node->id(), deps);

  return true;
}

void DepsLog::Close() {
  if (file_)
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

  bool valid_header = true;
  int version = 0;
  if (!fgets(buf, sizeof(buf), f) || fread(&version, 4, 1, f) < 1)
    valid_header = false;
  if (!valid_header || strcmp(buf, kFileSignature) != 0 ||
      version != kCurrentVersion) {
    *err = "bad deps log signature or version; starting over";
    fclose(f);
    unlink(path.c_str());
    // Don't report this as a failure.  An empty deps log will cause
    // us to rebuild the outputs anyway.
    return true;
  }

  long offset;
  bool read_failed = false;
  int unique_dep_record_count = 0;
  int total_dep_record_count = 0;
  for (;;) {
    offset = ftell(f);

    uint16_t size;
    if (fread(&size, 2, 1, f) < 1) {
      if (!feof(f))
        read_failed = true;
      break;
    }
    bool is_deps = (size >> 15) != 0;
    size = size & 0x7FFF;

    if (fread(buf, size, 1, f) < 1) {
      read_failed = true;
      break;
    }

    if (is_deps) {
      assert(size % 4 == 0);
      int* deps_data = reinterpret_cast<int*>(buf);
      int out_id = deps_data[0];
      int mtime = deps_data[1];
      deps_data += 2;
      int deps_count = (size / 4) - 2;

      Deps* deps = new Deps(mtime, deps_count);
      for (int i = 0; i < deps_count; ++i) {
        assert(deps_data[i] < (int)nodes_.size());
        assert(nodes_[deps_data[i]]);
        deps->nodes[i] = nodes_[deps_data[i]];
      }

      total_dep_record_count++;
      if (!UpdateDeps(out_id, deps))
        ++unique_dep_record_count;
    } else {
      StringPiece path(buf, size);
      Node* node = state->GetNode(path);
      assert(node->id() < 0);
      node->set_id(nodes_.size());
      nodes_.push_back(node);
    }
  }

  if (read_failed) {
    // An error occurred while loading; try to recover by truncating the
    // file to the last fully-read record.
    if (ferror(f)) {
      *err = strerror(ferror(f));
    } else {
      *err = "premature end of file";
    }
    fclose(f);

    if (!Truncate(path.c_str(), offset, err))
      return false;

    // The truncate succeeded; we'll just report the load error as a
    // warning because the build can proceed.
    *err += "; recovering";
    return true;
  }

  fclose(f);

  // Rebuild the log if there are too many dead records.
  int kMinCompactionEntryCount = 1000;
  int kCompactionRatio = 3;
  if (total_dep_record_count > kMinCompactionEntryCount &&
      total_dep_record_count > unique_dep_record_count * kCompactionRatio) {
    needs_recompaction_ = true;
  }

  return true;
}

DepsLog::Deps* DepsLog::GetDeps(Node* node) {
  // Abort if the node has no id (never referenced in the deps) or if
  // there's no deps recorded for the node.
  if (node->id() < 0 || node->id() >= (int)deps_.size())
    return NULL;
  return deps_[node->id()];
}

bool DepsLog::Recompact(const string& path, string* err) {
  METRIC_RECORD(".ninja_deps recompact");
  printf("Recompacting deps...\n");

  string temp_path = path + ".recompact";

  // OpenForWrite() opens for append.  Make sure it's not appending to a
  // left-over file from a previous recompaction attempt that crashed somehow.
  unlink(temp_path.c_str());

  DepsLog new_log;
  if (!new_log.OpenForWrite(temp_path, err))
    return false;

  // Clear all known ids so that new ones can be reassigned.  The new indices
  // will refer to the ordering in new_log, not in the current log.
  for (vector<Node*>::iterator i = nodes_.begin(); i != nodes_.end(); ++i)
    (*i)->set_id(-1);
  
  // Write out all deps again.
  for (int old_id = 0; old_id < (int)deps_.size(); ++old_id) {
    Deps* deps = deps_[old_id];
    if (!deps) continue;  // If nodes_[old_id] is a leaf, it has no deps.

    if (!new_log.RecordDeps(nodes_[old_id], deps->mtime,
                            deps->node_count, deps->nodes)) {
      new_log.Close();
      return false;
    }
  }

  new_log.Close();

  // All nodes now have ids that refer to new_log, so steal its data.
  deps_.swap(new_log.deps_);
  nodes_.swap(new_log.nodes_);

  if (unlink(path.c_str()) < 0) {
    *err = strerror(errno);
    return false;
  }

  if (rename(temp_path.c_str(), path.c_str()) < 0) {
    *err = strerror(errno);
    return false;
  }

  return true;
}

bool DepsLog::UpdateDeps(int out_id, Deps* deps) {
  if (out_id >= (int)deps_.size())
    deps_.resize(out_id + 1);

  bool delete_old = deps_[out_id] != NULL;
  if (delete_old)
    delete deps_[out_id];
  deps_[out_id] = deps;
  return delete_old;
}

bool DepsLog::RecordId(Node* node) {
  uint16_t size = (uint16_t)node->path().size();
  fwrite(&size, 2, 1, file_);
  fwrite(node->path().data(), node->path().size(), 1, file_);

  node->set_id(nodes_.size());
  nodes_.push_back(node);

  return true;
}
