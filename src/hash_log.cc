// Copyright 2014 Matthias Maennich (matthias@maennich.net).
// All Rights Reserved.
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

#include "hash_log.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "disk_interface.h"
#include "graph.h"
#include "hash_map.h"
#include "metrics.h"

using namespace std;

/// the default file name for the persisted hash log
const char     kHashLogFileName[] = ".ninja_hashes";

/// the file banner in the persisted hash log
static const char     kFileSignature[] = "# ninjahashlog\n";

/// the current hash log version, the datatype sizes
/// for time stamps and hashes and the maximum file path length
/// to detect incompatibilities
static const uint32_t kCurrentVersion  = 4;
static const uint32_t kHash_TSize = sizeof(HashLog::hash_t);
static const uint32_t kTimestampSize = sizeof(TimeStamp);
static const uint32_t kMaxFileNameLength = 1024;

HashLog::~HashLog() {
  Close();
}

bool HashLog::PutHash(const string &path, const hash_t hash,
                      const TimeStamp mtime, const hash_variant variant,
                      string* err) {
  if (file_ == NULL) {
    if (!Load(err)) {
      return false;
    }
  }

  // don't put any entry of too long paths
  if (path.size() > kMaxFileNameLength) {
    // as this is not an error, but a limitation, returning false is sufficient
    // indicating that no hash has been persisted, but there was also no real
    // error to deal with
    return false;
  }

  const key_t key(variant, path);
  // check whether we really need to push this entry (already there?)
  map_t::iterator it = hash_map_.lower_bound(key);
  if (it == hash_map_.end() || it->first         != key
                            || it->second.hash_  != hash
                            || it->second.mtime_ != mtime) {

    // persist the file path (null terminated)
    if (fputs(path.c_str(), file_) == EOF) {
      *err = strerror(errno);
      return false;
    }
    if (fputc('\0', file_) == EOF) {
      *err = strerror(errno);
      return false;
    }

    // persist the file hash
    if (fwrite(&hash, sizeof(hash), 1, file_) < 1) {
      *err = strerror(errno);
      return false;
    }

    // persist the modification time of the hashed file
    if (fwrite(&mtime, sizeof(mtime), 1, file_) < 1) {
      *err = strerror(errno);
      return false;
    }

    // persist the variant
    if (fwrite(&variant, sizeof(variant), 1, file_) < 1) {
      *err = strerror(errno);
      return false;
    }

    fflush(file_);

    // determine whether we just update the map or insert a new entry
    if (it != hash_map_.end() && it->first == key) {
      it->second.hash_  = hash;
      it->second.mtime_ = mtime;
    } else {
      hash_map_.insert(it, map_t::value_type(key, mapped_t(hash, mtime)));
    }
    ++total_values_;
  }
  return true;
}

bool HashLog::UpdateHash(Node* node, const hash_variant variant,
                         string* err, const bool force, hash_t* result) {
  // initialize the result
  if (result) { *result = 0; }

  if (file_ == NULL) {
    if (!Load(err)) {
      return false;
    }
  }

  // early exit for files with too long file name
  // if we would go ahead and let ninja stat the file, it will fail
  if (node->path().length() > kMaxFileNameLength) {
    return false;
  }

  if (!node->StatIfNecessary(disk_interface_, err)) {
    return false;
  }

  // early exit for non-existing files
  if (!node->exists()) {
    return false;
  }

  // check whether we need to update the hash

  // first, do we have an old hash? and has the modification time changed
  // since we recorded the old hash?
  const key_t key(variant, node->path());
  const map_t::const_iterator it = hash_map_.find(key);
  const bool mtime_changed =
          (it == hash_map_.end() || it->second.mtime_ != node->mtime());

  // we know the old hash
  if (result && it != hash_map_.end()) {
    *result = it->second.hash_;
  }

  const string& path = node->path();

  // based on the decision, create the hash and persist it
  if (force || mtime_changed) {
    const hash_t hash = disk_interface_->HashFile(path, err);
    if (!err->empty()) {
      return false;
    }
    if (PutHash(path, hash, node->mtime(), variant, err)) {
      if (result) { *result = hash; }
      return true;
    }
  }
  return false;
}

HashLog::hash_t HashLog::GetHash(Node* node, const hash_variant variant,
                                 string* err) {
  if (file_ == NULL) {
    if (!Load(err)) {
      return 0;
    }
  }
  const string& path = node->path();
  const map_t::const_iterator it = hash_map_.find(key_t(variant, path));
  if (it == hash_map_.end()) {
    return 0;
  } else {
    return it->second.hash_;
  }
}

bool HashLog::HashChanged(Node* node, const hash_variant variant, string* err) {
  if (file_ == NULL) {
    if (!Load(err)) {
      return true;
    }
  }

  // this is an early exit with cached results of this method
  std::map<Node*,bool>::const_iterator i = changed_files_.find(node);
  if (i != changed_files_.end()) {
    return i->second;
  }

  if (!node->StatIfNecessary(disk_interface_, err)) {
    return false;
  }

  const string& path = node->path();
  const map_t::const_iterator it = hash_map_.find(key_t(variant, path));

  bool result = false;

  // no hash in our hash log means we consider the hash changed
  if (it == hash_map_.end()) {
    changed_files_[node] = true;
    result = true;
  }

  // we only check the hash if the mtime of the file changed
  // since we last computed the hash
  if (!result && it->second.mtime_ != node->mtime()) {
    hash_t current_hash = disk_interface_->HashFile(path, err);
    if (!err->empty()) {
      return true;
    }
    result = it->second.hash_ != current_hash;
    PutHash(path, current_hash, node->mtime(), variant, err);
    if (!err->empty()) {
      return true;
    }
  }
  changed_files_[node] = result;
  return result;
}

bool HashLog::EdgeChanged(const Edge* edge, std::string* err) {
  // if the edge has no (non order only) deps or no outputs,
  // we cannot decide and exit early
  if (edge->inputs_.size() - edge->order_only_deps_ == 0
   || edge->outputs_.size() == 0) {
     return true;
  }

  hash_t target = 0;
  // we check first if any of the inputs have changed since we
  // ran last time. if so, we can exit early at this point already.
  for (vector<Node*>::const_iterator i = edge->inputs_.begin();
     i != edge->inputs_.end() - edge->order_only_deps_; ++i) {
    if (HashChanged(*i, SOURCE, err)) {
      return true;
    }
    target += GetHash(*i, SOURCE, err);
    if (!err->empty()) {
      return true; // in case of any errors we exit early and delegate the
                   // error handling (true === edge changed)
    }
  }

  // we also check the combined hash for the edge's outputs.
  // (even though all the inputs are unchanged, we still might have the case
  // that our Edge has not been built in the last run, hence updated files did
  // not influence any output of our build and therefore are subject to rebuild)
  for (vector<Node*>::const_iterator i = edge->outputs_.begin();
       i != edge->outputs_.end(); ++i) {
    key_t key(TARGET, (*i)->path());
    map_t::const_iterator it = hash_map_.find(key);
    if (!(*i)->StatIfNecessary(disk_interface_, err)) {
      return false;
    }
    if (it == hash_map_.end()
     || it->second.hash_ != target
     || it->second.mtime_ != (*i)->mtime()) {
      return true;
    }
  }

  return false;
}

void HashLog::EdgeFinished(const Edge *edge, std::string* err) {
  hash_t temp_hash = 0;
  hash_t target = 0;
  for (vector<Node*>::const_iterator i = edge->inputs_.begin();
       i != edge->inputs_.end() - edge->order_only_deps_; ++i) {
    UpdateHash(*i, SOURCE, err, false, &temp_hash);
    if (!err->empty()) {
      *err = "Error updating hash log: " + *err;
      return;
    }
    target += temp_hash;
  }
  for (vector<Node*>::const_iterator i = edge->outputs_.begin();
       i != edge->outputs_.end(); ++i) {
    TimeStamp mtime = disk_interface_->Stat((*i)->path(), err);
    if (mtime < 0)
      return;
    PutHash((*i)->path(), target, mtime, TARGET, err);
    if (!err->empty()) {
      *err = "Error updating hash log: " + *err;
      return;
    }
  }
}

bool HashLog::Recompact(string* err, const bool force) {
  // this roughly means the hashlog has 3 times the size of its actual needed
  // size
  if (force || total_values_ > 3 * hash_map_.size()) {
    METRIC_RECORD(string(kHashLogFileName) + " recompact");
    if (file_ == NULL) {
      if (!Load(err)) {
        return false;
      }
    }

    // we just throw away the old log and put all entries we have in again
    // fairly simple :-)

    const map_t old_hash_map = hash_map_;

    Close();
    unlink(filename_.c_str());
    Load(err);
    if (!err->empty()) {
      return false;
    }

    for (map_t::const_iterator I = old_hash_map.begin(),
                                                  E = old_hash_map.end();
                                                  I != E; ++I) {
      PutHash(I->first.val_, I->second.hash_, I->second.mtime_,
              I->first.variant_, err);
      if (!err->empty()) {
        return false;
      }
    }
    return true;
  }
  return false;
}

bool HashLog::Close() {
  if (file_) {
    fclose(file_);
    file_ = NULL;
    return true;
  }
  return false;
}

bool HashLog::Load(string* err) {
  // reset all the affected members
  assert(file_ == NULL);
  hash_map_.clear();
  changed_files_.clear();
  total_values_ = 0;

  METRIC_RECORD(string(kHashLogFileName) + " load");

  // open up the file
  char buf[sizeof(kFileSignature)];
  file_ = fopen(filename_.c_str(), "a+b");
  if (!file_) {
    *err = strerror(errno);
    return false;
  }

  // file header actions for new files

  // jump to the end of the file
  fseek(file_, 0, SEEK_END);

  // there was no file or the file was empty. we create the header
  if (ftell(file_) == 0) {
    // the header string
    if (fwrite(kFileSignature, sizeof(kFileSignature) - 1, 1, file_) < 1) {
      *err = strerror(errno);
      return false;
    }
    // the version int
    if (fwrite(&kCurrentVersion, sizeof(kCurrentVersion), 1, file_) < 1) {
      *err = strerror(errno);
      return false;
    }
    // the hash size used
    if (fwrite(&kHash_TSize, sizeof(kHash_TSize), 1, file_) < 1) {
      *err = strerror(errno);
      return false;
    }
    // the timestamp size used
    if (fwrite(&kTimestampSize, sizeof(kTimestampSize), 1, file_) < 1) {
      *err = strerror(errno);
      return false;
    }
    // the maximum filename length used
    if (fwrite(&kMaxFileNameLength, sizeof(kMaxFileNameLength), 1, file_) < 1) {
      *err = strerror(errno);
      return false;
    }
    // flush it to the file
    if (fflush(file_) != 0) {
      *err = strerror(errno);
      return false;
    }
  }

  // file header actions for all files (new/existing)

  // jump to the beginning of the file
  fseek(file_, 0, SEEK_SET);

  // load header
  bool valid_header = true;
  uint32_t version = 0;
  uint32_t hash_t_size = 0;
  uint32_t timestamp_size = 0;
  uint32_t maxfilename_size = 0;

  // validate the header
  if (!fgets(buf, sizeof(buf), file_)
         || fread(&version,          sizeof(version),          1, file_) < 1
         || fread(&hash_t_size,      sizeof(hash_t_size),      1, file_) < 1
         || fread(&timestamp_size,   sizeof(timestamp_size),   1, file_) < 1
         || fread(&maxfilename_size, sizeof(maxfilename_size), 1, file_) < 1 ) {
    valid_header = false;
  }
  if (!valid_header || strcmp(buf, kFileSignature) != 0
                    || version != kCurrentVersion
                    || hash_t_size != kHash_TSize
                    || timestamp_size != kTimestampSize
                    || maxfilename_size != kMaxFileNameLength) {
    *err = "incompatible hash log, resetting";
    Close();
    unlink(filename_.c_str());
    return Load(err);
  }

  // read path/hash/mtime/variant tuple stream

  char path_raw[kMaxFileNameLength];
  for (;;) {
    // read the null terminated path
    char* str = path_raw;
    int c;
    size_t count = 0;
    do {
      c = fgetc(file_);
      if (c == EOF) {
          break;
      } else {
        *str = (char) c;
        ++count;
      }
    } while((*str++ != '\0') && (count < kMaxFileNameLength));
    bool key_read = (c != EOF && count < kMaxFileNameLength);
    std::string path(path_raw);

    // read the hash
    hash_t hash = 0;
    const size_t val_read = fread(&hash, sizeof(hash_t), 1, file_);

    // read the mtime
    TimeStamp mtime = 0;
    const size_t mtime_read = fread(&mtime, sizeof(TimeStamp), 1, file_);

    // read the variant
    hash_variant variant = UNDEFINED;
    const size_t variant_read = fread(&variant, sizeof(hash_variant), 1, file_);

    // consistency check
    // either we read a value for every element or we read nothing at all
    // in case this is inconsistent, we consider the log to be corrupt
    if (!key_read || val_read != 1 || mtime_read != 1 || variant_read != 1) {
      if (!key_read            && count   == 0
          && val_read     == 0 && hash    == 0
          && mtime_read   == 0 && mtime   == 0
          && variant_read == 0 && variant == UNDEFINED
          && feof(file_)) {
        // all fine -- all values are untouched and we see eof
        break;
      } else {
        // the log is not consistent / corrupt
        *err = "the log was corrupted, resetting";
        Close();
        unlink(filename_.c_str());
        return Load(err);
      }
    } else { // all fine, ready to store it internally
      const key_t key(variant, path);
      map_t::iterator it = hash_map_.lower_bound(key);
      if (it != hash_map_.end() && it->first == key) {
        it->second.hash_  = hash;
        it->second.mtime_ = mtime;
      } else {
        hash_map_.insert(it, map_t::value_type(key, mapped_t(hash, mtime)));
      }
      ++total_values_;
    }
  }

  Recompact(err);

  SetCloseOnExec(fileno(file_));

  if (!err->empty()) {
    return false;
  }

  return true;
}
