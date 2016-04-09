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

#include <string>
#include <sstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

#include "graph.h"
#include "hash_log.h"
#include "test.h"

void wait(uint32_t millis) {
#ifdef _WIN32
  Sleep(millis);
#else
  timespec t;
  t.tv_sec = millis / 1000;
  t.tv_nsec = (millis%1000)*1000*1000;
  nanosleep(&t, NULL);
#endif
}


const char kTestFilename[] = "HashLogTest-tempfile";
const char kTestInput1[] = "HashLogTest-testinput1";
const char kTestInput2[] = "HashLogTest-testinput2";
const char kTestInput3[] = "HashLogTest-testinput3";
const char kTestOutput1[] = "HashLogTest-testoutput1";
const char kTestOutput2[] = "HashLogTest-testoutput2";
const char kTestOutput3[] = "HashLogTest-testoutput3";

struct HashLogTest : public testing::Test {
  HashLogTest()
    : disk_interface(new RealDiskInterface())
    , log(kTestFilename, disk_interface)
    , state()
    , err()
    , in_node1()
    , in_node2()
    , out_node1()
    , out_node2()
    , empty_edge()
    , edge_without_inputs()
    , edge_without_outputs()
    , edge_1_1()
    , edge_2_1()
    , edge_1_2()
    , edge_2_2() {

    in_node1 = state.GetNode(kTestInput1, 0);
    in_node2 = state.GetNode(kTestInput2, 0);
    in_node3 = state.GetNode(kTestInput3, 0);
    out_node1 = state.GetNode(kTestOutput1, 0);
    out_node2 = state.GetNode(kTestOutput2, 0);
    out_node3 = state.GetNode(kTestOutput3, 0);

    edge_without_inputs.outputs_.push_back(out_node1);
    edge_without_outputs.inputs_.push_back(in_node1);

    edge_1_1.inputs_.push_back(in_node1);
    edge_1_1.outputs_.push_back(out_node1);

    edge_2_1.inputs_.push_back(in_node1);
    edge_2_1.inputs_.push_back(in_node2);
    edge_2_1.outputs_.push_back(out_node1);

    edge_1_2.inputs_.push_back(in_node1);
    edge_1_2.outputs_.push_back(out_node1);
    edge_1_2.outputs_.push_back(out_node2);

    edge_2_2.inputs_.push_back(in_node1);
    edge_2_2.inputs_.push_back(in_node2);
    edge_2_2.outputs_.push_back(out_node1);
    edge_2_2.outputs_.push_back(out_node2);
  }
  void cleanup() {
    unlink(kTestFilename);
    unlink(kTestInput1);
    unlink(kTestInput2);
    unlink(kTestOutput1);
    unlink(kTestOutput2);
  }

  void dummy_content() {
     disk_interface->WriteFile(kTestInput1, "testinput1");
     disk_interface->WriteFile(kTestInput2, "testinput2");
     disk_interface->WriteFile(kTestInput3, "testinput3");
     disk_interface->WriteFile(kTestOutput1, "testoutput1");
     disk_interface->WriteFile(kTestOutput2, "testoutput2");
     disk_interface->WriteFile(kTestOutput3, "testoutput3");
  }

  virtual void SetUp() { cleanup(); };
  virtual void TearDown() { cleanup(); };
  virtual ~HashLogTest() { delete disk_interface; }

  DiskInterface* disk_interface;
  HashLog log;
  State state;
  string err;

  Node* in_node1;
  Node* in_node2;
  Node* in_node3;
  Node* out_node1;
  Node* out_node2;
  Node* out_node3;

  Edge empty_edge;
  Edge edge_without_inputs;
  Edge edge_without_outputs;
  Edge edge_1_1;
  Edge edge_2_1;
  Edge edge_1_2;
  Edge edge_2_2;
};

TEST_F(HashLogTest, MapKeyTest) {
  // serialization will break if this is changing
  ASSERT_EQ(HashLog::UNDEFINED, 0);
  ASSERT_EQ(HashLog::SOURCE, 1);
  ASSERT_EQ(HashLog::TARGET, 2);

  // comparison for enum instances
  for (int i = HashLog::UNDEFINED; i != HashLog::TARGET; ++i) {
    HashLog::key_t key1(static_cast<HashLog::hash_variant>(i), "test1");
    HashLog::key_t key2(static_cast<HashLog::hash_variant>(i), "test1");
    HashLog::key_t key3(static_cast<HashLog::hash_variant>(i), "test2");
    ASSERT_EQ(key1, key2);
    ASSERT_NE(key1, key3);
  }
  // test stable order of enum values
  {
    HashLog::key_t key1(HashLog::UNDEFINED, "test1");
    HashLog::key_t key2(HashLog::SOURCE, "test1");
    HashLog::key_t key3(HashLog::TARGET, "test1");

    ASSERT_TRUE(key1 < key2);
    ASSERT_TRUE(key2 < key3);
    ASSERT_TRUE(key1 < key3);
  }

  {
    HashLog::key_t key1(HashLog::UNDEFINED, "test1");
    HashLog::key_t key2(HashLog::UNDEFINED, "test2");
    HashLog::key_t key3(HashLog::UNDEFINED, "test3");

    ASSERT_TRUE(key1 < key2);
    ASSERT_TRUE(key2 < key3);
    ASSERT_TRUE(key1 < key3);
  }
}

TEST_F(HashLogTest, BasicInOut) {
  // file does not exist yet
  Node* node = state.GetNode(kTestInput1, 0);
  // hash is zero as file does not exist and no hash has been recorded yet
  ASSERT_EQ(0u, log.GetHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // the file does not exist, hence there should not be an hash to be updated
  ASSERT_FALSE(log.UpdateHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // write the dummy file
  disk_interface->WriteFile(kTestInput1, "test");
  // still no hash to find as there has nothing been recorded
  ASSERT_EQ(0u, log.GetHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // still no update as the stat lookup went into cache
  ASSERT_FALSE(log.UpdateHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_EQ(0u, log.GetHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // now reset the stat to recognize the change
  node->ResetState();
  ASSERT_TRUE(log.UpdateHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_NE(0u, log.GetHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
}

TEST_F(HashLogTest, HashCrossCheck) {
  {
    // write a file with dummy content and update its hash
    disk_interface->WriteFile(kTestInput1, "test1");
    Node* node = state.GetNode(kTestInput1, 0);
    ASSERT_TRUE(log.UpdateHash(node, HashLog::UNDEFINED, &err));
    ASSERT_TRUE(err.empty());

    // cross check with disk_interface hash
    ASSERT_EQ(log.GetHash(node, HashLog::UNDEFINED, &err),
              disk_interface->HashFile(node->path(), &err));
    ASSERT_TRUE(err.empty());
  }
  {
    // check with a non-existent file
    Node* node = state.GetNode(kTestInput2, 0);
    ASSERT_FALSE(log.UpdateHash(node, HashLog::UNDEFINED, &err));
    ASSERT_TRUE(err.empty());

    // cross check with disk_interface hash (not existent is _not_ hash == 0)
    ASSERT_NE(log.GetHash(node, HashLog::UNDEFINED, &err),
              disk_interface->HashFile(node->path(), &err));
    ASSERT_TRUE(err.empty());

    // now create an empty file and update the hash
    disk_interface->WriteFile(kTestInput2, "");
    node->ResetState();
    ASSERT_TRUE(log.UpdateHash(node, HashLog::UNDEFINED, &err));
    ASSERT_TRUE(err.empty());

    // cross check with disk_interface hash (not existent === empty file)
    ASSERT_EQ(log.GetHash(node, HashLog::UNDEFINED, &err),
              disk_interface->HashFile("hashlog-not-existing-file", &err));
    ASSERT_TRUE(err.empty());
  }
}

TEST_F(HashLogTest, UpdateGet) {
  // preperation
  disk_interface->WriteFile(kTestInput1, "test1");
  disk_interface->WriteFile(kTestInput2, "test2");
  Node* node1 = state.GetNode(kTestInput1, 0);
  Node* node2 = state.GetNode(kTestInput2, 0);

  /// SIMPLE TESTS

  // get value1 from empty log
  ASSERT_EQ(0u, log.GetHash(node1, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // write value1 to log
  ASSERT_TRUE(log.UpdateHash(node1, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // get written value1 from log
  const HashLog::hash_t node1_hash =
          log.GetHash(node1, HashLog::UNDEFINED, &err);
  ASSERT_TRUE(err.empty());
  ASSERT_NE(0u,node1_hash);

  // get value2 from empty log
  ASSERT_EQ(0u, log.GetHash(node2, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // write value2 to log
  ASSERT_TRUE(log.UpdateHash(node2, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // get written value2 from log
  const HashLog::hash_t node2_hash =
          log.GetHash(node2, HashLog::UNDEFINED, &err);
  ASSERT_TRUE(err.empty());
  ASSERT_NE(0u,node2_hash);

  ASSERT_NE(node1_hash, node2_hash);

  /// UPDATE, FORCE UPDATE, LAZY UPDATE

  // update file2 to have the same content
  disk_interface->WriteFile(kTestInput2, "test1");

  // get hash is still unchanged (no update)
  ASSERT_EQ(log.GetHash(node2, HashLog::UNDEFINED, &err), node2_hash);
  ASSERT_TRUE(err.empty());

  // update the hash (not forced)
  ASSERT_FALSE(log.UpdateHash(node2, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // get hash is still unchanged (no forced update)
  ASSERT_EQ(log.GetHash(node2, HashLog::UNDEFINED, &err), node2_hash);
  ASSERT_TRUE(err.empty());

  // update the hash (forced)
  ASSERT_TRUE(log.UpdateHash(node2, HashLog::UNDEFINED, &err, true));
  ASSERT_TRUE(err.empty());

  // now the hash is changed ...
  ASSERT_NE(log.GetHash(node2, HashLog::UNDEFINED, &err), node2_hash);
  ASSERT_TRUE(err.empty());
  // ... to the same value as node1
  ASSERT_EQ(log.GetHash(node2, HashLog::UNDEFINED, &err), node1_hash);
  ASSERT_TRUE(err.empty());

  // updating again can only be done forcefully
  ASSERT_FALSE(log.UpdateHash(node2, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_TRUE(log.UpdateHash(node2, HashLog::UNDEFINED, &err, true));
  ASSERT_TRUE(err.empty());

  // update file2 again
  disk_interface->WriteFile(kTestInput2, "test2");
  // not-forced update does not change anything
  ASSERT_FALSE(log.UpdateHash(node2, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_EQ(log.GetHash(node2, HashLog::UNDEFINED, &err), node1_hash); // node1!
  ASSERT_TRUE(err.empty());

  // reset the state such that UpdateHash does the stat implicitely
  node2->ResetState();
  // still the not forced update is not effective, because we did it already
  ASSERT_FALSE(log.UpdateHash(node2, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_EQ(log.GetHash(node2, HashLog::UNDEFINED, &err), node1_hash); // node1!
  ASSERT_TRUE(err.empty());

  // but the forced one is
  ASSERT_TRUE(log.UpdateHash(node2, HashLog::UNDEFINED, &err, true));
  ASSERT_TRUE(err.empty());
  ASSERT_EQ(log.GetHash(node2, HashLog::UNDEFINED, &err), node2_hash); // node2!
  ASSERT_TRUE(err.empty());

  // write the same file again with the same content
  disk_interface->WriteFile(kTestInput2, "test2");
  // stat is cached, so no update
  ASSERT_FALSE(log.UpdateHash(node2, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
  // reset the stat
  node2->ResetState();
  // still no update, as we did this in this lifetime already
  ASSERT_FALSE(log.UpdateHash(node2, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
}

TEST_F(HashLogTest, LoadClose) {
  Node* node = state.GetNode(kTestInput1, 0);
  disk_interface->WriteFile(kTestInput1, "test1");
  Node* node2 = state.GetNode(kTestInput2, 0);
  disk_interface->WriteFile(kTestInput2, "test2");

  // should not be in the log (implicitely opening log)
  ASSERT_EQ(0u, log.GetHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // close the log
  ASSERT_TRUE(log.Close());

  // close the log (again) should do nothing
  ASSERT_FALSE(log.Close());

  // update value1 (should open the log)
  ASSERT_TRUE(log.UpdateHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // check whether the value is in
  ASSERT_NE(0u, log.GetHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // close the log
  ASSERT_TRUE(log.Close()); ASSERT_FALSE(log.Close());

  // check whether the value is still in (implicit reopen)
  ASSERT_NE(0u, log.GetHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // close the log
  ASSERT_TRUE(log.Close()); ASSERT_FALSE(log.Close());

  // update value2 (should open the log)
  ASSERT_TRUE(log.UpdateHash(node2, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // close the log
  ASSERT_TRUE(log.Close()); ASSERT_FALSE(log.Close());

  // check whether the value is still in (implicit reopen)
  ASSERT_NE(0u, log.GetHash(node2, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // close the log
  ASSERT_TRUE(log.Close()); ASSERT_FALSE(log.Close());

  // update a hash that has been updated in the previous life of the log
  // reopening the log does not invalidate this fact
  ASSERT_FALSE(log.UpdateHash(node2, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // close the log
  ASSERT_TRUE(log.Close()); ASSERT_FALSE(log.Close());

  wait(1000); // mtime is significant
  disk_interface->WriteFile(kTestInput2, "test3");
  node2->ResetState();
  // update a hash that has been updated in the previous life of the log
  // this time the file really changed
  ASSERT_TRUE(log.UpdateHash(node2, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

}

TEST_F(HashLogTest, Variants) {
  Node* node = state.GetNode(kTestInput1, 0);
  disk_interface->WriteFile(kTestInput1, "test1");

  // should be empty for all variants
  ASSERT_EQ(0u, log.GetHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_EQ(0u, log.GetHash(node, HashLog::SOURCE, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_EQ(0u, log.GetHash(node, HashLog::TARGET, &err));
  ASSERT_TRUE(err.empty());

  // now update the hash in SOURCE
  ASSERT_TRUE(log.UpdateHash(node, HashLog::SOURCE, &err));
  ASSERT_TRUE(err.empty());

  // should be only changed for SOURCE
  ASSERT_EQ(0u, log.GetHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_NE(0u, log.GetHash(node, HashLog::SOURCE, &err));   // NE!
  ASSERT_TRUE(err.empty());
  ASSERT_EQ(0u, log.GetHash(node, HashLog::TARGET, &err));
  ASSERT_TRUE(err.empty());

  // update it for another variant (TARGET)
  ASSERT_TRUE(log.UpdateHash(node, HashLog::TARGET, &err));
  ASSERT_TRUE(err.empty());

  // should be only changed for SOURCE and TARGET
  ASSERT_EQ(0u, log.GetHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_NE(0u, log.GetHash(node, HashLog::SOURCE, &err));   // NE!
  ASSERT_TRUE(err.empty());
  ASSERT_NE(0u, log.GetHash(node, HashLog::TARGET, &err));   // NE!
  ASSERT_TRUE(err.empty());
}

void check_reset(HashLog& log, Node* node) {
  string err;
  // get value 1 (not in log)
  ASSERT_EQ(0u, log.GetHash(node, HashLog::UNDEFINED, &err));
  ASSERT_FALSE(err.empty());
  err = "";

  // update value 1
  ASSERT_TRUE(log.UpdateHash(node, HashLog::UNDEFINED, &err, true));
  ASSERT_TRUE(err.empty());

  // get value 1 (now it should be there)
  ASSERT_NE(0u, log.GetHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // close log
  ASSERT_TRUE(log.Close());

  // get value 1 (now from reopened undamaged log)
  ASSERT_NE(0u, log.GetHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // close log
  ASSERT_TRUE(log.Close());

}

TEST_F(HashLogTest, Consistency) {

  // update the hash for file1
  Node* node = state.GetNode(kTestInput1, 0);
  disk_interface->WriteFile(kTestInput1, "test1");
  ASSERT_TRUE(log.UpdateHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_TRUE(log.Close());

  // corrupt log (destroy header)
  {
    string content;
    DiskInterface::Status status = disk_interface->ReadFile(kTestFilename, &content, &err);
    ASSERT_EQ(status, DiskInterface::Okay);
    ASSERT_TRUE(err.empty());
    ASSERT_FALSE(content.empty());
    content.replace(content.find("ninja"),string("ninja").length(), "nanja");
    disk_interface->WriteFile(kTestFilename, content);
    ASSERT_TRUE(err.empty());
  }

  // the corrupt log is now loaded and reset as the corrupt state has been
  // discovered on load. hence we expect an empty log.
  check_reset(log, node);

  // corrupt log (append garbage)
  {
    string content;
    DiskInterface::Status status = disk_interface->ReadFile(kTestFilename, &content, &err);
    ASSERT_EQ(status, DiskInterface::Okay);
    ASSERT_TRUE(err.empty());
    content.append("XX\0");
    disk_interface->WriteFile(kTestFilename, content);
    ASSERT_TRUE(err.empty());
  }

  // the corrupt log is now loaded and reset as the corrupt state has been
  // discovered on load. hence we expect an empty log.
  check_reset(log, node);

  // corrupt log (write an incomplete line)
  {
    FILE* file = fopen(kTestFilename, "a+b");
    ASSERT_TRUE(file);
    ASSERT_NE(fputs("asdf", file), EOF); // the file path
    ASSERT_NE(fputc('\0', file), EOF); // null terminated
    // uncomplete line until here
    fclose(file);
  }

  // the corrupt log is now loaded and reset as the corrupt state has been
  // discovered on load. hence we expect an empty log.
  check_reset(log, node);
}

TEST_F(HashLogTest, CornerCases) {
  // try to add a file with too long name
  stringstream ss;
  for (size_t i = 0; i < 2048; ++i) {
     ss << "a";
  }
  Node* node_long = state.GetNode(ss.str(), 0);

  // try to put hash for file with too long file name
  ASSERT_FALSE(log.UpdateHash(node_long, HashLog::SOURCE, &err, true));
  ASSERT_TRUE(err.empty());
  ASSERT_FALSE(log.UpdateHash(node_long, HashLog::SOURCE, &err));
  ASSERT_TRUE(err.empty());
}

TEST_F(HashLogTest, HashChanged) {
  Node* node = state.GetNode(kTestInput1, 0);

  // file does not exist, hence we expect 'hash has changed'
  ASSERT_TRUE(log.HashChanged(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // we create the file and update the hash in the log
  disk_interface->WriteFile(kTestInput1, "test1");
  node->ResetState(); // as the above HashChanged did a stat already
  ASSERT_TRUE(log.UpdateHash(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // now we hit the early exit, file has been checked in this lifetime
  ASSERT_TRUE(log.HashChanged(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // close the log to end the lifetime
  ASSERT_TRUE(log.Close());

  // check again (after reopening the log)
  ASSERT_FALSE(log.HashChanged(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // we change the file content and ask for whether it has changed
  wait(1000); // mtime is significant
  disk_interface->WriteFile(kTestInput1, "test2");
  // this time we hit the cache again
  ASSERT_FALSE(log.HashChanged(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
  // reset the stat
  node->ResetState();
  // we still hit the cache
  ASSERT_FALSE(log.HashChanged(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
  // close the log to end the lifetime
  ASSERT_TRUE(log.Close());
  // now we get the real information
  ASSERT_TRUE(log.HashChanged(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
  // asking again delivers the cached result
  ASSERT_TRUE(log.HashChanged(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());

  // close the log to end the lifetime
  ASSERT_TRUE(log.Close());
  // asking again delivers the 'hash has not changed'
  ASSERT_FALSE(log.HashChanged(node, HashLog::UNDEFINED, &err));
  ASSERT_TRUE(err.empty());
}

TEST_F(HashLogTest, UnchangedEdges) {
  // edges without inputs or without outputs are considered always changed
  ASSERT_TRUE(log.EdgeChanged(&empty_edge, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_TRUE(log.EdgeChanged(&edge_without_inputs, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_TRUE(log.EdgeChanged(&edge_without_outputs, &err));
  ASSERT_TRUE(err.empty());

  // not yet finished edges should also be considered changed
  ASSERT_TRUE(log.EdgeChanged(&edge_1_1, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_TRUE(log.EdgeChanged(&edge_2_1, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_TRUE(log.EdgeChanged(&edge_1_2, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_TRUE(log.EdgeChanged(&edge_2_2, &err));
  ASSERT_TRUE(err.empty());

  // we close the log to simulate a new log lifetime
  ASSERT_TRUE(log.Close());

  // not yet finished edges should still be considered changed
  ASSERT_TRUE(log.EdgeChanged(&edge_1_1, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_TRUE(log.EdgeChanged(&edge_2_1, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_TRUE(log.EdgeChanged(&edge_1_2, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_TRUE(log.EdgeChanged(&edge_2_2, &err));
  ASSERT_TRUE(err.empty());

}

TEST_F(HashLogTest, SimpleFinishedEdges) {
  dummy_content();

  /// T1
  // not yet finished edges should be considered changed
  ASSERT_TRUE(log.EdgeChanged(&edge_1_1, &err));
  ASSERT_TRUE(err.empty());

  // set the edge to finished
  log.EdgeFinished(&edge_1_1, &err);
  ASSERT_TRUE(err.empty());

  // asking in the same log life time will hit the cache
  ASSERT_TRUE(log.EdgeChanged(&edge_1_1, &err));
  ASSERT_TRUE(err.empty());

  ASSERT_TRUE(log.Close());

  /// T2
  // asking after reopening should tell unchanged
  ASSERT_FALSE(log.EdgeChanged(&edge_1_1, &err));
  ASSERT_TRUE(err.empty());

  ASSERT_TRUE(log.Close());

  /// T3
  // rewrite all files
  dummy_content();
  // only reset the state of the input file
  in_node1->ResetState();
  ASSERT_FALSE(log.EdgeChanged(&edge_1_1, &err));
  ASSERT_TRUE(err.empty());

  ASSERT_TRUE(log.Close());

  /// T4
  // really write new content to input1
  wait(1000);
  disk_interface->WriteFile(kTestInput1, "blubb");
  in_node1->ResetState();
  ASSERT_TRUE(log.EdgeChanged(&edge_1_1, &err));
  ASSERT_TRUE(err.empty());
}

TEST_F(HashLogTest, SkippedRun){
  dummy_content();

  /// T1
  // simulate a run with hashing
  log.EdgeFinished(&edge_2_2, &err);
  ASSERT_TRUE(err.empty());

  ASSERT_TRUE(log.Close());

  /// T2
  // simulate a run without hashing (inputs do not matter)
  wait(1000); // mtime of output is significant
   // write all files
  dummy_content();
  // reset stat of one of the outputs
  out_node1->ResetState();

  /// T3
  // actually no input changed the content, but the modification time
  // of our output does not matched the stat time of the EdgeFinished call
  // hence we expect the Edge to be changed
  ASSERT_TRUE(log.EdgeChanged(&edge_2_2, &err));

  // run1 with hash, run2 without, run3 with hash
  ASSERT_TRUE(err.empty());
}

void influence_test(HashLog log, Edge& edge1, Edge& edge2) {
  string err;
  // not yet finished edges should be considered changed
  ASSERT_TRUE(log.EdgeChanged(&edge1, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_TRUE(log.EdgeChanged(&edge2, &err));
  ASSERT_TRUE(err.empty());

  // finish edge_2 in t1
  log.EdgeFinished(&edge2, &err);
  ASSERT_TRUE(err.empty());

  // go to t2
  ASSERT_TRUE(log.Close());

  // only edge_1 is changed and has to be rebuilt
  ASSERT_TRUE(log.EdgeChanged(&edge1, &err));
  ASSERT_TRUE(err.empty());
  ASSERT_FALSE(log.EdgeChanged(&edge2, &err));
  ASSERT_TRUE(err.empty());

}

TEST_F(HashLogTest, InfluencingEdgesSimple) {
  dummy_content();

  // edge_1 == out1 : in1
  // edge_2 == out2 : in1
  // rebuilding edge_2 in t1 should not eliminate rebuilding edge_1 in t2

  Edge edge_1, edge_2;
  edge_1.outputs_.push_back(out_node1);
  edge_1.inputs_.push_back(in_node1);

  edge_2.outputs_.push_back(out_node2);
  edge_2.inputs_.push_back(in_node1);

  influence_test(log, edge_1, edge_2);

}

TEST_F(HashLogTest, InfluencingEdgesMultiIn) {
  dummy_content();

  // edge_1 == out1 : in1
  // edge_2 == out2 : in1, in2
  // rebuilding edge_2 in t1 should not eliminate rebuilding edge_1 in t2

  Edge edge_1, edge_2;
  edge_1.outputs_.push_back(out_node1);
  edge_1.inputs_.push_back(in_node1);

  edge_2.outputs_.push_back(out_node2);
  edge_2.inputs_.push_back(in_node1);
  edge_2.inputs_.push_back(in_node2);

  influence_test(log, edge_1, edge_2);
}

TEST_F(HashLogTest, InfluencingEdgesMultiInOut) {
  dummy_content();

  // edge_1 == out1       : in1
  // edge_2 == out2, out3 : in1, in2
  // rebuilding edge_2 in t1 should not eliminate rebuilding edge_1 in t2

  Edge edge_1, edge_2;
  edge_1.outputs_.push_back(out_node1);
  edge_1.inputs_.push_back(in_node1);

  edge_2.outputs_.push_back(out_node2);
  edge_2.outputs_.push_back(out_node3);
  edge_2.inputs_.push_back(in_node1);
  edge_2.inputs_.push_back(in_node2);

  influence_test(log, edge_1, edge_2);
}

TEST_F(HashLogTest, Recompact) {
  disk_interface->WriteFile(kTestInput1, "test");

  Node* node1 = state.GetNode(kTestInput1, 0);

  ASSERT_FALSE(log.Recompact(&err));

  ASSERT_TRUE(log.UpdateHash(node1, HashLog::SOURCE, &err));
  ASSERT_TRUE(err.empty());

  ASSERT_FALSE(log.Recompact(&err));

  // force update some hashes
  for (uint32_t i = 0 ; i < 3; ++i) {

    ASSERT_TRUE(log.UpdateHash(node1, HashLog::SOURCE, &err, true));
    ASSERT_TRUE(err.empty());
  }

  // these do not blow up the log, so no recompacting necessary
  ASSERT_FALSE(log.Recompact(&err));

  // update some more hashes
  for (uint32_t i = 0 ; i < 3; ++i) {

    stringstream ss("test");
    ss << i;
    disk_interface->WriteFile(kTestInput1, ss.str());
    ASSERT_TRUE(log.UpdateHash(node1, HashLog::SOURCE, &err, true));
    ASSERT_TRUE(err.empty());
  }

  // now recompacting is necessary
  ASSERT_TRUE(log.Recompact(&err));

}
